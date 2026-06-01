from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    gas_params = os.path.join(get_package_share_directory('gas_monitor'), 'config', 'gas_params.yaml')
    cam_params = os.path.join(get_package_share_directory('thermal_camera_monitor'), 'config', 'camera_params.yaml')
    alarm_params = os.path.join(get_package_share_directory('alarm_manager'), 'config', 'alarm_params.yaml')

    return LaunchDescription([
        Node(package='gas_monitor', executable='serial_gas_node', name='serial_gas_node', output='screen', parameters=[gas_params]),
        Node(package='gas_monitor', executable='http_gas_node', name='http_gas_node', output='screen', parameters=[gas_params]),
        Node(package='thermal_camera_monitor', executable='hik_alarm_node', name='hik_alarm_node', output='screen', parameters=[cam_params]),
        Node(package='alarm_manager', executable='alarm_aggregator_node', name='alarm_aggregator_node', output='screen', parameters=[alarm_params]),
    ])
