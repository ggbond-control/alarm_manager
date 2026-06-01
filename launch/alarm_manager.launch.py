from launch import LaunchDescription
from launch_ros.actions import Node
from ament_index_python.packages import get_package_share_directory
import os


def generate_launch_description():
    params = os.path.join(get_package_share_directory('alarm_manager'), 'config', 'alarm_params.yaml')
    return LaunchDescription([
        Node(
            package='alarm_manager',
            executable='alarm_aggregator_node',
            name='alarm_aggregator_node',
            output='screen',
            parameters=[params],
        )
    ])
