#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>

#include "ament_index_cpp/get_package_share_directory.hpp"
#include "rclcpp/rclcpp.hpp"
#include "diagnostic_msgs/msg/diagnostic_array.hpp"
#include "diagnostic_msgs/msg/diagnostic_status.hpp"
#include "monitor_interfaces/msg/alarm_event.hpp"
#include "monitor_interfaces/msg/gas_alarm_event.hpp"
#include "monitor_interfaces/msg/thermal_camera_event.hpp"

using namespace std::chrono_literals;

class AlarmAggregatorNode : public rclcpp::Node
{
public:
    AlarmAggregatorNode() : Node("alarm_aggregator_node")
    {
        gas_alarm_audio_ = declare_parameter<std::string>("gas_alarm_audio", "gasLeakage.wav");
        image_alarm_audio_ = declare_parameter<std::string>("image_alarm_audio", "bodyDetect.wav");
        save_pic_dir_ = declare_parameter<std::string>("save_pic_dir", "alarms");
        min_interval_seconds_ = declare_parameter<int>("min_interval_seconds", 2);

        const auto pkg_share = ament_index_cpp::get_package_share_directory("alarm_manager");
        gas_alarm_audio_ = resolve_audio_path(pkg_share, gas_alarm_audio_);
        image_alarm_audio_ = resolve_audio_path(pkg_share, image_alarm_audio_);
        save_pic_dir_ = resolve_save_dir(pkg_share, save_pic_dir_);
        std::filesystem::create_directories(save_pic_dir_);

        event_pub_ = create_publisher<monitor_interfaces::msg::AlarmEvent>("/monitor/alarm/event", 10);
        health_pub_ = create_publisher<diagnostic_msgs::msg::DiagnosticArray>("/monitor/alarm/health", 10);

        gas_sub_ = create_subscription<monitor_interfaces::msg::GasAlarmEvent>(
            "/monitor/gas/serial/alarm_event", 10,
            std::bind(&AlarmAggregatorNode::on_gas_event, this, std::placeholders::_1));

        camera_sub_ = create_subscription<monitor_interfaces::msg::ThermalCameraEvent>(
            "/monitor/camera/thermal_event", 10,
            std::bind(&AlarmAggregatorNode::on_camera_event, this, std::placeholders::_1));

        health_timer_ = create_wall_timer(5s, std::bind(&AlarmAggregatorNode::publish_health, this));
        RCLCPP_INFO(get_logger(), "报警管理节点已启动：气体音频=%s 图像音频=%s 图片目录=%s 最小间隔=%d秒",
                    gas_alarm_audio_.c_str(), image_alarm_audio_.c_str(), save_pic_dir_.c_str(), min_interval_seconds_);
    }

private:
    static constexpr const char *kLogRed = "\033[31m";
    static constexpr const char *kLogReset = "\033[0m";

    static std::string resolve_audio_path(const std::string &pkg_share, const std::string &param_path)
    {
        std::filesystem::path p(param_path);
        if (p.is_absolute())
        {
            return p.string();
        }
        return (std::filesystem::path(pkg_share) / "audio" / p).string();
    }

    std::string resolve_save_dir(const std::string &pkg_share, const std::string &param_path)
    {
        std::filesystem::path p(param_path);
        if (p.is_absolute())
        {
            return p.string();
        }

        const std::filesystem::path share_path(pkg_share);
        std::filesystem::path workspace_root;
        for (auto it = share_path.begin(); it != share_path.end(); ++it)
        {
            if (*it == "install")
                break;
            workspace_root /= *it;
        }

        if (!workspace_root.empty() && std::filesystem::exists(workspace_root / "src" / "alarm_manager"))
        {
            return (workspace_root / "src" / "alarm_manager" / p).string();
        }

        RCLCPP_WARN(get_logger(), "无法从安装目录推导工作区源码路径，图片将保存到安装目录。");
        return (share_path / p).string();
    }

    void on_gas_event(const monitor_interfaces::msg::GasAlarmEvent::SharedPtr msg)
    {
        if (msg->audible)
            play_sound(gas_alarm_audio_);
        publish_alarm("gas", "gas_" + msg->level, msg->detail, msg->audible ? gas_alarm_audio_ : "", false, "", false, "");
    }

    void on_camera_event(const monitor_interfaces::msg::ThermalCameraEvent::SharedPtr msg)
    {
        if (!allow_trigger("camera"))
            return;

        std::string event_type = "camera_alarm";
        std::string detail = msg->raw_summary.empty() ? "检测到摄像机报警" : msg->raw_summary;
        if (msg->command == 0x5212)
        {
            event_type = "thermal_alarm";
            if (msg->raw_summary.empty())
                detail = "检测到热成像测温报警";
        }
        else if (msg->command == 0x5211)
        {
            event_type = "thermal_diff_alarm";
            if (msg->raw_summary.empty())
                detail = "检测到热成像温差报警";
        }
        else if (msg->command == 0x4993)
        {
            event_type = "camera_image_alarm";
            if (msg->raw_summary.empty())
                detail = "检测到摄像机智能报警";
        }

        std::string image_path;
        bool image_saved = false;
        if (!msg->image_data.empty())
        {
            image_path = save_image_from_buffer(msg->image_data, "visible");
            image_saved = !image_path.empty();
        }

        std::string thermal_image_path;
        bool thermal_image_saved = false;
        if (!msg->thermal_image_data.empty())
        {
            thermal_image_path = save_image_from_buffer(msg->thermal_image_data, "thermal");
            thermal_image_saved = !thermal_image_path.empty();
        }

        play_sound(image_alarm_audio_);
        publish_alarm("camera", event_type, detail, image_alarm_audio_, image_saved, image_path, thermal_image_saved, thermal_image_path);
    }

    bool allow_trigger(const std::string &source)
    {
        const auto now_tp = std::chrono::steady_clock::now();
        auto it = last_trigger_time_.find(source);
        if (it == last_trigger_time_.end())
        {
            last_trigger_time_[source] = now_tp;
            return true;
        }

        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now_tp - it->second).count();
        if (elapsed < min_interval_seconds_)
            return false;

        it->second = now_tp;
        return true;
    }

    void play_sound(const std::string &file_path)
    {
        if (access(file_path.c_str(), F_OK) != 0)
        {
            RCLCPP_WARN(get_logger(), "未找到音频文件：%s", file_path.c_str());
            return;
        }

        std::string cmd = "aplay -q \"" + file_path + "\" &";
        int ret = std::system(cmd.c_str());
        if (ret != 0)
        {
            cmd = "paplay \"" + file_path + "\" &";
            ret = std::system(cmd.c_str());
            if (ret != 0)
            {
                RCLCPP_WARN(get_logger(), "播放音频失败：%s", file_path.c_str());
            }
        }
    }

    std::string save_image_from_buffer(const std::vector<uint8_t> &data, const std::string &prefix)
    {
        int offset = -1;
        for (size_t i = 0; i + 1 < data.size(); i++)
        {
            if (data[i] == 0xFF && data[i + 1] == 0xD8)
            {
                offset = static_cast<int>(i);
                break;
            }
        }
        if (offset < 0)
        {
            RCLCPP_WARN(get_logger(), "摄像机报警数据中未找到 JPEG 起始标记，跳过保存图片。");
            return "";
        }

        const auto now_time = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now_time);
        std::tm tm_buf{};
        localtime_r(&t, &tm_buf);

        std::ostringstream oss;
        oss << save_pic_dir_ << "/" << prefix << "_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".jpg";

        std::ofstream out(oss.str(), std::ios::binary);
        if (!out.is_open())
        {
            RCLCPP_WARN(get_logger(), "无法写入报警图片：%s", oss.str().c_str());
            return "";
        }

        out.write(reinterpret_cast<const char *>(data.data() + offset), static_cast<std::streamsize>(data.size() - offset));
        out.close();
        return oss.str();
    }

    void publish_alarm(const std::string &source, const std::string &event_type, const std::string &detail,
                       const std::string &audio_file, bool image_saved, const std::string &image_path,
                       bool thermal_image_saved, const std::string &thermal_image_path)
    {
        monitor_interfaces::msg::AlarmEvent msg;
        msg.header.stamp = now();
        msg.header.frame_id = "alarm_manager";
        msg.source = source;
        msg.event_type = event_type;
        msg.detail = detail;
        msg.audio_file = audio_file;
        msg.image_saved = image_saved;
        msg.image_path = image_path;
        msg.thermal_image_saved = thermal_image_saved;
        msg.thermal_image_path = thermal_image_path;
        event_pub_->publish(msg);
        RCLCPP_INFO(get_logger(), "%s已发布报警事件：来源=%s 类型=%s 详情=%s%s",
                    kLogRed, source.c_str(), event_type.c_str(), detail.c_str(), kLogReset);
    }

    void publish_health()
    {
        diagnostic_msgs::msg::DiagnosticArray arr;
        arr.header.stamp = now();

        diagnostic_msgs::msg::DiagnosticStatus status;
        status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
        status.name = "alarm_manager";
        status.message = "运行中";

        arr.status.push_back(status);
        health_pub_->publish(arr);
    }

    std::string gas_alarm_audio_;
    std::string image_alarm_audio_;
    std::string save_pic_dir_;
    int min_interval_seconds_{};

    std::map<std::string, std::chrono::steady_clock::time_point> last_trigger_time_;

    rclcpp::Publisher<monitor_interfaces::msg::AlarmEvent>::SharedPtr event_pub_;
    rclcpp::Publisher<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr health_pub_;

    rclcpp::Subscription<monitor_interfaces::msg::GasAlarmEvent>::SharedPtr gas_sub_;
    rclcpp::Subscription<monitor_interfaces::msg::ThermalCameraEvent>::SharedPtr camera_sub_;
    rclcpp::TimerBase::SharedPtr health_timer_;
};

int main(int argc, char **argv)
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<AlarmAggregatorNode>();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
