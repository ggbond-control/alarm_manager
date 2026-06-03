#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "diagnostic_msgs/msg/key_value.hpp"
#include "monitor_interfaces/action/execute_alarm.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_action/rclcpp_action.hpp"

class AlarmAggregatorNode : public rclcpp::Node
{
public:
    using ExecuteAlarm = monitor_interfaces::action::ExecuteAlarm;
    using GoalHandleExecuteAlarm = rclcpp_action::ServerGoalHandle<ExecuteAlarm>;

    AlarmAggregatorNode() : Node("alarm_aggregator_node")
    {
        audio_base_dir_ = declare_parameter<std::string>("audio_base_dir", "audio");
        player_command_ = declare_parameter<std::string>("player_command", "aplay");
        fallback_player_command_ = declare_parameter<std::string>("fallback_player_command", "paplay");

        package_share_ = ament_index_cpp::get_package_share_directory("alarm_manager");
        audio_base_dir_ = resolve_base_dir(audio_base_dir_);

        action_server_ = rclcpp_action::create_server<ExecuteAlarm>(
            this,
            "/monitor/alarm/execute",
            std::bind(&AlarmAggregatorNode::handle_goal, this, std::placeholders::_1, std::placeholders::_2),
            std::bind(&AlarmAggregatorNode::handle_cancel, this, std::placeholders::_1),
            std::bind(&AlarmAggregatorNode::handle_accepted, this, std::placeholders::_1));

        RCLCPP_INFO(get_logger(), "报警执行 action 已就绪：/monitor/alarm/execute 音频目录=%s", audio_base_dir_.c_str());
    }

private:
    rclcpp_action::GoalResponse handle_goal(const rclcpp_action::GoalUUID &, std::shared_ptr<const ExecuteAlarm::Goal> goal)
    {
        if (goal->command != ExecuteAlarm::Goal::COMMAND_PLAY_AUDIO && goal->command != ExecuteAlarm::Goal::COMMAND_STOP_AUDIO)
        {
            RCLCPP_WARN(get_logger(), "拒绝未知报警执行命令：%u", goal->command);
            return rclcpp_action::GoalResponse::REJECT;
        }
        return rclcpp_action::GoalResponse::ACCEPT_AND_EXECUTE;
    }

    rclcpp_action::CancelResponse handle_cancel(const std::shared_ptr<GoalHandleExecuteAlarm>)
    {
        stop_players();
        return rclcpp_action::CancelResponse::ACCEPT;
    }

    void handle_accepted(const std::shared_ptr<GoalHandleExecuteAlarm> goal_handle)
    {
        std::thread([this, goal_handle]()
                    { execute_goal(goal_handle); })
            .detach();
    }

    void execute_goal(const std::shared_ptr<GoalHandleExecuteAlarm> goal_handle)
    {
        const auto goal = goal_handle->get_goal();
        publish_feedback(goal_handle, ExecuteAlarm::Goal::STATE_EXECUTING, "正在执行报警措施", {});

        auto result = std::make_shared<ExecuteAlarm::Result>();
        result->header.stamp = now();
        result->header.frame_id = "alarm_manager";

        if (goal->command == ExecuteAlarm::Goal::COMMAND_STOP_AUDIO)
        {
            stop_players();
            fill_result(*result, true, ExecuteAlarm::Goal::STATE_COMPLETED, "已停止报警音频", {});
            goal_handle->succeed(result);
            return;
        }

        const std::string audio_path = resolve_audio_uri(goal->audio_uri);
        std::vector<diagnostic_msgs::msg::KeyValue> values = {
            kv("alarm_type", goal->alarm_type),
            kv("detail", goal->detail),
            kv("audio_uri", goal->audio_uri),
            kv("resolved_audio_path", audio_path)};

        if (audio_path.empty())
        {
            fill_result(*result, false, ExecuteAlarm::Goal::STATE_ERROR, "音频路径为空", values);
            goal_handle->abort(result);
            return;
        }
        if (access(audio_path.c_str(), F_OK) != 0)
        {
            fill_result(*result, false, ExecuteAlarm::Goal::STATE_ERROR, "未找到音频文件：" + audio_path, values);
            goal_handle->abort(result);
            return;
        }

        const bool ok = play_sound(audio_path);
        if (ok)
        {
            fill_result(*result, true, ExecuteAlarm::Goal::STATE_COMPLETED, "报警音频已开始播放", values);
            publish_feedback(goal_handle, ExecuteAlarm::Goal::STATE_COMPLETED, "报警音频已开始播放", values);
            goal_handle->succeed(result);
        }
        else
        {
            fill_result(*result, false, ExecuteAlarm::Goal::STATE_ERROR, "播放音频失败：" + audio_path, values);
            goal_handle->abort(result);
        }
    }

    static diagnostic_msgs::msg::KeyValue kv(const std::string &key, const std::string &value)
    {
        diagnostic_msgs::msg::KeyValue item;
        item.key = key;
        item.value = value;
        return item;
    }

    void publish_feedback(const std::shared_ptr<GoalHandleExecuteAlarm> &goal_handle,
                          uint8_t state,
                          const std::string &message,
                          const std::vector<diagnostic_msgs::msg::KeyValue> &values)
    {
        auto feedback = std::make_shared<ExecuteAlarm::Feedback>();
        feedback->header.stamp = now();
        feedback->header.frame_id = "alarm_manager";
        feedback->state = state;
        feedback->message = message;
        feedback->values = values;
        goal_handle->publish_feedback(feedback);
    }

    void fill_result(ExecuteAlarm::Result &result,
                     bool success,
                     uint8_t state,
                     const std::string &message,
                     const std::vector<diagnostic_msgs::msg::KeyValue> &values)
    {
        result.header.stamp = now();
        result.header.frame_id = "alarm_manager";
        result.success = success;
        result.final_state = state;
        result.message = message;
        result.values = values;
    }

    std::string resolve_base_dir(const std::string &param_path) const
    {
        std::filesystem::path p(param_path);
        if (p.is_absolute())
            return p.string();
        return (std::filesystem::path(package_share_) / p).string();
    }

    std::string resolve_audio_uri(const std::string &uri) const
    {
        if (uri.empty())
            return "";

        const std::string package_prefix = "package://";
        if (uri.rfind(package_prefix, 0) == 0)
        {
            const auto rest = uri.substr(package_prefix.size());
            const auto slash = rest.find('/');
            if (slash == std::string::npos)
                return "";
            const auto package_name = rest.substr(0, slash);
            const auto relative_path = rest.substr(slash + 1);
            return (std::filesystem::path(ament_index_cpp::get_package_share_directory(package_name)) / relative_path).string();
        }

        std::filesystem::path p(uri);
        if (p.is_absolute())
            return p.string();

        return (std::filesystem::path(audio_base_dir_) / p).string();
    }

    bool play_sound(const std::string &file_path)
    {
        std::string cmd = player_command_ + " -q \"" + file_path + "\" &";
        int ret = std::system(cmd.c_str());
        if (ret == 0)
            return true;

        cmd = fallback_player_command_ + " \"" + file_path + "\" &";
        ret = std::system(cmd.c_str());
        return ret == 0;
    }

    void stop_players()
    {
        if (!player_command_.empty())
        {
            const std::filesystem::path player(player_command_);
            const std::string name = player.filename().string();
            if (!name.empty())
                killall(name);
        }
        if (!fallback_player_command_.empty())
        {
            const std::filesystem::path player(fallback_player_command_);
            const std::string name = player.filename().string();
            if (!name.empty())
                killall(name);
        }
    }

    static void killall(const std::string &process_name)
    {
        const std::string cmd = "pkill -x \"" + process_name + "\" >/dev/null 2>&1";
        const int ret = std::system(cmd.c_str());
        (void)ret;
    }

    std::string package_share_;
    std::string audio_base_dir_;
    std::string player_command_;
    std::string fallback_player_command_;
    rclcpp_action::Server<ExecuteAlarm>::SharedPtr action_server_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<AlarmAggregatorNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
