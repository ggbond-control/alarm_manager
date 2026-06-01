#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
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
#include "monitor_interfaces/msg/gas_leak_event.hpp"
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

        gas_sub_ = create_subscription<monitor_interfaces::msg::GasLeakEvent>(
            "/monitor/gas/http/leak_event", 10,
            std::bind(&AlarmAggregatorNode::on_gas_event, this, std::placeholders::_1));

        camera_sub_ = create_subscription<monitor_interfaces::msg::ThermalCameraEvent>(
            "/monitor/camera/thermal_event", 10,
            std::bind(&AlarmAggregatorNode::on_camera_event, this, std::placeholders::_1));

        health_timer_ = create_wall_timer(5s, std::bind(&AlarmAggregatorNode::publish_health, this));
    }

private:
    static std::string resolve_audio_path(const std::string &pkg_share, const std::string &param_path)
    {
        std::filesystem::path p(param_path);
        if (p.is_absolute())
        {
            return p.string();
        }
        return (std::filesystem::path(pkg_share) / "audio" / p).string();
    }

    static std::string resolve_save_dir(const std::string &pkg_share, const std::string &param_path)
    {
        std::filesystem::path p(param_path);
        if (p.is_absolute())
        {
            return p.string();
        }
        return (std::filesystem::path(pkg_share) / p).string();
    }

    void on_gas_event(const monitor_interfaces::msg::GasLeakEvent::SharedPtr msg)
    {
        if (!msg->leaking)
        {
            publish_alarm("gas", "gas_normalized", msg->detail, "", false, "");
            return;
        }

        if (!allow_trigger("gas"))
            return;
        play_sound(gas_alarm_audio_);
        publish_alarm("gas", "gas_leak", msg->detail, gas_alarm_audio_, false, "");
    }

    void on_camera_event(const monitor_interfaces::msg::ThermalCameraEvent::SharedPtr msg)
    {
        if (!allow_trigger("camera"))
            return;

        std::string event_type = "camera_alarm";
        if (msg->command == 0x4993)
            event_type = "thermal_alarm";
        else if (msg->command == 0x5212)
            event_type = "camera_image_alarm";

        std::string image_path;
        bool image_saved = false;
        if (msg->has_image && !msg->image_data.empty())
        {
            image_path = save_image_from_buffer(msg->image_data);
            image_saved = !image_path.empty();
        }

        play_sound(image_alarm_audio_);
        publish_alarm("camera", event_type, msg->raw_summary, image_alarm_audio_, image_saved, image_path);
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
            RCLCPP_WARN(get_logger(), "Audio file not found: %s", file_path.c_str());
            return;
        }

        std::string cmd = "aplay -q \"" + file_path + "\" &";
        int ret = std::system(cmd.c_str());
        if (ret != 0)
        {
            cmd = "paplay \"" + file_path + "\" &";
            (void)std::system(cmd.c_str());
        }
    }

    std::string save_image_from_buffer(const std::vector<uint8_t> &data)
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
            return "";

        const auto now_time = std::chrono::system_clock::now();
        const auto t = std::chrono::system_clock::to_time_t(now_time);
        std::tm tm_buf{};
        localtime_r(&t, &tm_buf);

        std::ostringstream oss;
        oss << save_pic_dir_ << "/img_" << std::put_time(&tm_buf, "%Y%m%d_%H%M%S") << ".jpg";

        std::ofstream out(oss.str(), std::ios::binary);
        if (!out.is_open())
            return "";

        out.write(reinterpret_cast<const char *>(data.data() + offset), static_cast<std::streamsize>(data.size() - offset));
        out.close();
        return oss.str();
    }

    void publish_alarm(const std::string &source, const std::string &event_type, const std::string &detail,
                       const std::string &audio_file, bool image_saved, const std::string &image_path)
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
        event_pub_->publish(msg);
    }

    void publish_health()
    {
        diagnostic_msgs::msg::DiagnosticArray arr;
        arr.header.stamp = now();

        diagnostic_msgs::msg::DiagnosticStatus status;
        status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
        status.name = "alarm_manager";
        status.message = "running";

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

    rclcpp::Subscription<monitor_interfaces::msg::GasLeakEvent>::SharedPtr gas_sub_;
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
