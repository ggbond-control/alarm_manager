import os

from ament_index_python.packages import get_package_share_directory
from launch import LaunchDescription
from launch.actions import SetEnvironmentVariable
from launch_ros.actions import Node


def generate_launch_description():
    gas_params = os.path.join(
        get_package_share_directory('gas_monitor'), 'config', 'gas_params.yaml'
    )
    sensor_gimbal_config = os.path.join(
        get_package_share_directory('sensor_gimbal'), 'config'
    )
    gimbal_params = os.path.join(sensor_gimbal_config, 'stub_params.yaml')
    cam_params = os.path.join(sensor_gimbal_config, 'camera_params.yaml')
    alarm_params = os.path.join(
        get_package_share_directory('alarm_manager'), 'config', 'alarm_params.yaml'
    )

    return LaunchDescription([
        SetEnvironmentVariable('RCUTILS_CONSOLE_OUTPUT_FORMAT', '[{name}]: {message}'),
        SetEnvironmentVariable('RCUTILS_COLORIZED_OUTPUT', '1'),
        Node(
            package='gas_monitor',
            executable='serial_gas_node',
            name='serial_gas_node',
            output='screen',
            output_format='{line}',
            parameters=[gas_params],
        ),
        Node(
            package='sensor_gimbal',
            executable='sensor_gimbal_node',
            output='screen',
            output_format='{line}',
            parameters=[gimbal_params, cam_params, {
                'enable_gimbal': False,
                'enable_thermal': True,
                'enable_smoke': False,
            }],
        ),
        Node(
            package='alarm_manager',
            executable='alarm_aggregator_node',
            name='alarm_aggregator_node',
            output='screen',
            output_format='{line}',
            parameters=[alarm_params],
        ),
    ])
