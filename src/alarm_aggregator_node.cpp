#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <signal.h>
#include <map>
#include <mutex>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <vector>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "rcl_interfaces/msg/set_parameters_result.hpp"
#include "rclcpp/rclcpp.hpp"

class AlarmAggregatorNode : public rclcpp::Node
{
public:
    AlarmAggregatorNode() : Node("alarm_aggregator_node")
    {
        package_share_ = ament_index_cpp::get_package_share_directory("alarm_manager");

        audio_base_dir_ = declare_parameter<std::string>("audio_base_dir", "audio");
        player_command_ = declare_parameter<std::string>("player_command", "aplay");
        fallback_player_command_ = declare_parameter<std::string>("fallback_player_command", "paplay");
        gas_audio_ = declare_parameter<std::string>("gas_audio", "gasLeakage.wav");
        camera_audio_ = declare_parameter<std::string>("camera_audio", "bodyDetect.wav");
        alarm_category_ = declare_parameter<std::string>("alarm_category", "");
        play_ = declare_parameter<bool>("play", false);

        audio_base_dir_ = resolve_base_dir(audio_base_dir_);
        playback_thread_ = std::thread([this]() { playback_loop(); });
        set_params_handle_ = add_on_set_parameters_callback(std::bind(&AlarmAggregatorNode::on_set_parameters, this, std::placeholders::_1));

        RCLCPP_INFO(get_logger(), "报警喇叭参数服务已就绪：/alarm_aggregator_node/set_parameters 音频目录=%s 分类={gas:%s,camera:%s}",
                    audio_base_dir_.c_str(), gas_audio_.c_str(), camera_audio_.c_str());
    }

    ~AlarmAggregatorNode() override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutdown_requested_ = true;
            clear_pending_locked();
            stop_active_player_locked();
        }
        cv_.notify_all();
        if (playback_thread_.joinable())
            playback_thread_.join();
    }

private:
    rcl_interfaces::msg::SetParametersResult on_set_parameters(const std::vector<rclcpp::Parameter> &parameters)
    {
        std::lock_guard<std::mutex> lock(mutex_);

        auto next_audio_base_dir = audio_base_dir_;
        auto next_player_command = player_command_;
        auto next_fallback_player_command = fallback_player_command_;
        auto next_gas_audio = gas_audio_;
        auto next_camera_audio = camera_audio_;
        auto next_alarm_category = alarm_category_;
        auto next_play = play_;
        bool play_param_seen = false;
        bool alarm_category_param_seen = false;

        rcl_interfaces::msg::SetParametersResult result;
        result.successful = true;

        for (const auto &param : parameters)
        {
            try
            {
                if (param.get_name() == "audio_base_dir")
                {
                    next_audio_base_dir = resolve_base_dir(param.as_string());
                }
                else if (param.get_name() == "player_command")
                {
                    next_player_command = param.as_string();
                }
                else if (param.get_name() == "fallback_player_command")
                {
                    next_fallback_player_command = param.as_string();
                }
                else if (param.get_name() == "gas_audio")
                {
                    next_gas_audio = param.as_string();
                }
                else if (param.get_name() == "camera_audio")
                {
                    next_camera_audio = param.as_string();
                }
                else if (param.get_name() == "alarm_category")
                {
                    next_alarm_category = param.as_string();
                    alarm_category_param_seen = true;
                }
                else if (param.get_name() == "play")
                {
                    next_play = param.as_bool();
                    play_param_seen = true;
                }
            }
            catch (const rclcpp::ParameterTypeException &ex)
            {
                result.successful = false;
                result.reason = "参数类型错误：" + param.get_name() + " " + ex.what();
                return result;
            }
        }

        bool should_enqueue_alarm = false;
        bool wait_for_alarm_category = false;
        if (play_param_seen && next_play)
        {
            if (!next_alarm_category.empty())
            {
                should_enqueue_alarm = true;
            }
            else
            {
                wait_for_alarm_category = true;
            }
        }
        else if (alarm_category_param_seen && pending_play_without_category_ && next_play)
        {
            should_enqueue_alarm = true;
        }

        if (should_enqueue_alarm)
        {
            const std::string audio_path = resolve_audio_for_category(next_alarm_category, next_audio_base_dir, next_gas_audio, next_camera_audio);
            if (audio_path.empty())
            {
                result.successful = false;
                result.reason = "未知报警类别：" + next_alarm_category;
                return result;
            }
            if (access(audio_path.c_str(), F_OK) != 0)
            {
                result.successful = false;
                result.reason = "未找到报警音频文件：" + audio_path;
                return result;
            }
        }

        audio_base_dir_ = next_audio_base_dir;
        player_command_ = next_player_command;
        fallback_player_command_ = next_fallback_player_command;
        gas_audio_ = next_gas_audio;
        camera_audio_ = next_camera_audio;
        alarm_category_ = next_alarm_category;
        play_ = next_play;

        if (play_param_seen && !next_play)
        {
            pending_play_without_category_ = false;
            clear_pending_locked();
            stop_active_player_locked();
            cv_.notify_all();
            RCLCPP_INFO(get_logger(), "已停止报警音频并清空待播队列。");
        }
        else if (should_enqueue_alarm)
        {
            pending_play_without_category_ = false;
            enqueue_alarm_locked(next_alarm_category);
            cv_.notify_all();
        }
        else if (wait_for_alarm_category)
        {
            pending_play_without_category_ = true;
            RCLCPP_INFO(get_logger(), "收到播放请求，等待 alarm_category 参数后再播放。");
        }

        return result;
    }

    std::string resolve_base_dir(const std::string &param_path) const
    {
        std::filesystem::path p(param_path);
        if (p.is_absolute())
            return p.string();
        return (std::filesystem::path(package_share_) / p).string();
    }

    static std::string resolve_audio_path(const std::string &base_dir, const std::string &audio_file)
    {
        if (audio_file.empty())
            return "";

        const std::string package_prefix = "package://";
        if (audio_file.rfind(package_prefix, 0) == 0)
        {
            const auto rest = audio_file.substr(package_prefix.size());
            const auto slash = rest.find('/');
            if (slash == std::string::npos)
                return "";
            const auto package_name = rest.substr(0, slash);
            const auto relative_path = rest.substr(slash + 1);
            return (std::filesystem::path(ament_index_cpp::get_package_share_directory(package_name)) / relative_path).string();
        }

        std::filesystem::path p(audio_file);
        if (p.is_absolute())
            return p.string();

        return (std::filesystem::path(base_dir) / p).string();
    }

    static std::string resolve_audio_for_category(const std::string &category,
                                                  const std::string &base_dir,
                                                  const std::string &gas_audio,
                                                  const std::string &camera_audio)
    {
        if (category == "gas")
            return resolve_audio_path(base_dir, gas_audio);
        if (category == "camera" || category == "thermal_camera")
            return resolve_audio_path(base_dir, camera_audio);
        return "";
    }

    void enqueue_alarm_locked(const std::string &category)
    {
        const auto duplicate = std::find(pending_categories_.begin(), pending_categories_.end(), category);
        if (duplicate == pending_categories_.end() && active_category_ != category)
        {
            pending_categories_.push_back(category);
            RCLCPP_INFO(get_logger(), "报警音频请求已入队：类别=%s pending=%zu active=%s",
                        category.c_str(), pending_categories_.size(),
                        active_player_pid_ > 0 ? active_category_.c_str() : "none");
        }
        else
        {
            RCLCPP_DEBUG(get_logger(), "报警音频请求已合并：类别=%s pending=%zu active=%s",
                         category.c_str(), pending_categories_.size(),
                         active_player_pid_ > 0 ? active_category_.c_str() : "none");
        }
    }

    void clear_pending_locked()
    {
        pending_categories_.clear();
    }

    void stop_active_player_locked()
    {
        if (active_player_pid_ <= 0)
            return;

        kill(active_player_pid_, SIGTERM);
        active_player_pid_ = -1;
        active_category_.clear();
    }

    pid_t start_player_for_alarm(const std::string &file_path, const std::string &player_command,
                                 const std::string &fallback_player_command)
    {
        pid_t pid = launch_player(player_command, file_path, true);
        if (pid > 0)
            return pid;

        return launch_player(fallback_player_command, file_path, false);
    }

    void playback_loop()
    {
        while (rclcpp::ok())
        {
            std::string category;
            std::string audio_path;
            std::string player_command;
            std::string fallback_player_command;

            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]() {
                    return shutdown_requested_ || !pending_categories_.empty();
                });

                if (shutdown_requested_)
                    return;

                category = pending_categories_.front();
                pending_categories_.erase(pending_categories_.begin());
                audio_path = resolve_audio_for_category(category, audio_base_dir_, gas_audio_, camera_audio_);
                player_command = player_command_;
                fallback_player_command = fallback_player_command_;
            }

            if (audio_path.empty() || access(audio_path.c_str(), F_OK) != 0)
            {
                RCLCPP_WARN(get_logger(), "跳过报警音频：类别=%s 文件无效=%s",
                            category.c_str(), audio_path.c_str());
                continue;
            }

            const pid_t pid = start_player_for_alarm(audio_path, player_command, fallback_player_command);
            if (pid <= 0)
            {
                RCLCPP_WARN(get_logger(), "播放报警音频失败：类别=%s 文件=%s", category.c_str(), audio_path.c_str());
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (shutdown_requested_)
                {
                    kill(pid, SIGTERM);
                    waitpid(pid, nullptr, 0);
                    return;
                }
                active_player_pid_ = pid;
                active_category_ = category;
                RCLCPP_INFO(get_logger(), "开始播放报警音频：类别=%s 文件=%s", category.c_str(), audio_path.c_str());
            }

            waitpid(pid, nullptr, 0);

            {
                std::lock_guard<std::mutex> lock(mutex_);
                const bool completed_active_player = active_player_pid_ == pid;
                if (completed_active_player)
                {
                    active_player_pid_ = -1;
                    active_category_.clear();
                }

                if (completed_active_player && play_ && alarm_category_ == category)
                {
                    enqueue_alarm_locked(category);
                    cv_.notify_all();
                    RCLCPP_INFO(get_logger(), "报警音频播放结束并继续循环：类别=%s pending=%zu",
                                category.c_str(), pending_categories_.size());
                }
                else
                {
                    RCLCPP_INFO(get_logger(), "报警音频播放结束：类别=%s pending=%zu play=%s current_category=%s",
                                category.c_str(), pending_categories_.size(),
                                play_ ? "true" : "false", alarm_category_.c_str());
                }
            }
        }
    }

    static pid_t launch_player(const std::string &player_command, const std::string &file_path, bool quiet_flag)
    {
        if (player_command.empty())
            return -1;

        const pid_t pid = fork();
        if (pid < 0)
            return -1;

        if (pid == 0)
        {
            FILE *null_stdout = freopen("/dev/null", "w", stdout);
            FILE *null_stderr = freopen("/dev/null", "w", stderr);
            (void)null_stdout;
            (void)null_stderr;

            if (quiet_flag)
                execlp(player_command.c_str(), player_command.c_str(), "-q", file_path.c_str(), static_cast<char *>(nullptr));
            else
                execlp(player_command.c_str(), player_command.c_str(), file_path.c_str(), static_cast<char *>(nullptr));
            _exit(127);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        int status = 0;
        const pid_t wait_ret = waitpid(pid, &status, WNOHANG);
        if (wait_ret == 0)
            return pid;

        return -1;
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::thread playback_thread_;
    std::string package_share_;
    std::string audio_base_dir_;
    std::string player_command_;
    std::string fallback_player_command_;
    std::string gas_audio_;
    std::string camera_audio_;
    std::string alarm_category_;
    bool play_{false};
    bool pending_play_without_category_{false};
    bool shutdown_requested_{false};
    std::vector<std::string> pending_categories_;
    std::string active_category_;
    pid_t active_player_pid_{-1};
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr set_params_handle_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<AlarmAggregatorNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
