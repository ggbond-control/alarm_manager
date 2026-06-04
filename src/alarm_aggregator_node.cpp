#include <unistd.h>

#include <chrono>
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
        set_params_handle_ = add_on_set_parameters_callback(std::bind(&AlarmAggregatorNode::on_set_parameters, this, std::placeholders::_1));

        RCLCPP_INFO(get_logger(), "报警喇叭参数服务已就绪：/alarm_aggregator_node/set_parameters 音频目录=%s 分类={gas:%s,camera:%s}",
                    audio_base_dir_.c_str(), gas_audio_.c_str(), camera_audio_.c_str());
    }

    ~AlarmAggregatorNode() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_players();
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

        if (play_param_seen && next_play)
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
            stop_players();
            if (!play_sound(audio_path, next_player_command, next_fallback_player_command))
            {
                result.successful = false;
                result.reason = "播放报警音频失败：" + audio_path;
                return result;
            }
            RCLCPP_INFO(get_logger(), "已播放报警音频：类别=%s 文件=%s", next_alarm_category.c_str(), audio_path.c_str());
        }
        else if (play_param_seen && !next_play)
        {
            stop_players();
            RCLCPP_INFO(get_logger(), "已停止报警音频。");
        }

        audio_base_dir_ = next_audio_base_dir;
        player_command_ = next_player_command;
        fallback_player_command_ = next_fallback_player_command;
        gas_audio_ = next_gas_audio;
        camera_audio_ = next_camera_audio;
        alarm_category_ = next_alarm_category;
        play_ = next_play;
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

    bool play_sound(const std::string &file_path, const std::string &player_command, const std::string &fallback_player_command)
    {
        pid_t pid = launch_player(player_command, file_path, true);
        if (pid > 0)
        {
            active_player_pid_ = pid;
            return true;
        }

        pid = launch_player(fallback_player_command, file_path, false);
        if (pid > 0)
        {
            active_player_pid_ = pid;
            return true;
        }

        return false;
    }

    void stop_players()
    {
        if (active_player_pid_ <= 0)
            return;

        kill(active_player_pid_, SIGTERM);
        for (int i = 0; i < 10; ++i)
        {
            if (waitpid(active_player_pid_, nullptr, WNOHANG) == active_player_pid_)
            {
                active_player_pid_ = -1;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        kill(active_player_pid_, SIGKILL);
        waitpid(active_player_pid_, nullptr, 0);
        active_player_pid_ = -1;
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
    std::string package_share_;
    std::string audio_base_dir_;
    std::string player_command_;
    std::string fallback_player_command_;
    std::string gas_audio_;
    std::string camera_audio_;
    std::string alarm_category_;
    bool play_{false};
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
