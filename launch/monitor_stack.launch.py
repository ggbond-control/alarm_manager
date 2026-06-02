from launch import LaunchDescription
from launch.actions import SetEnvironmentVariable
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    gas_params = os.path.join(get_package_share_directory('gas_monitor'), 'config', 'gas_params.yaml')
    cam_params = os.path.join(get_package_share_directory('thermal_camera_monitor'), 'config', 'camera_params.yaml')
    alarm_params = os.path.join(get_package_share_directory('alarm_manager'), 'config', 'alarm_params.yaml')

    return LaunchDescription([
        SetEnvironmentVariable('RCUTILS_CONSOLE_OUTPUT_FORMAT', '[{name}]: {message}'),
        SetEnvironmentVariable('RCUTILS_COLORIZED_OUTPUT', '1'),
        Node(package='gas_monitor', executable='serial_gas_node', name='serial_gas_node', output='screen', output_format='{line}', parameters=[gas_params]),
        Node(package='thermal_camera_monitor', executable='hik_alarm_node', name='hik_alarm_node', output='screen', output_format='{line}', parameters=[cam_params]),
        Node(package='alarm_manager', executable='alarm_aggregator_node', name='alarm_aggregator_node', output='screen', output_format='{line}', parameters=[alarm_params]),
    ])
