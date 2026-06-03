# 警报管理器

## 编译

```bash
cd ~/Workspace/algor_ws
colcon build --packages-select monitor_interfaces gas_monitor thermal_camera_monitor alarm_manager --symlink-install
```

## 启动

1. gas_monitor：

   ```bash
   source ~/Workspace/algor_ws/install/setup.zsh
   ros2 run gas_monitor serial_gas_node
   ros2 action send_goal /monitor/gas/serial/monitor monitor_interfaces/action/MonitorDevice "{command: 1, device_type: 'gas', feedback_interval: {sec: 1, nanosec: 0}, parameters: []}" --feedback
   ```

2. thermal_camera_monitor：

   ```bash
   source ~/Workspace/algor_ws/install/setup.zsh
   ros2 run thermal_camera_monitor hik_alarm_node
   ros2 action send_goal /monitor/camera/thermal/monitor monitor_interfaces/action/MonitorDevice "{command: 1, device_type: 'thermal_camera', feedback_interval: {sec: 1, nanosec: 0}, parameters: []}" --feedback
   ```

3. alarm_manager：

   ```bash
   source ~/Workspace/algor_ws/install/setup.zsh
   ros2 run alarm_manager alarm_aggregator_node
   ros2 action send_goal /monitor/alarm/execute monitor_interfaces/action/ExecuteAlarm "{command: 1, alarm_type: 'gas_leakage', detail: 'test', audio_uri: 'gasLeakage.wav', duration: {sec: 0, nanosec: 0}, parameters: []}" --feedback
   ```

4. all：

   ```bash
   source ~/Workspace/algor_ws/install/setup.zsh
   ros2 launch alarm_manager monitor_stack.launch.py
   ```

## 接口

| 模块                   | Action名称                        | Action类型                                |
| ---------------------- | --------------------------------- | ----------------------------------------- |
| gas_monitor            | `/monitor/gas/serial/monitor`     | `monitor_interfaces/action/MonitorDevice` |
| thermal_camera_monitor | `/monitor/camera/thermal/monitor` | `monitor_interfaces/action/MonitorDevice` |
| alarm_manager          | `/monitor/alarm/execute`          | `monitor_interfaces/action/ExecuteAlarm`  |

## 排错

1. 气体传感器：

   ```bash
   ls -l /dev/ttyUSB0
   crw-rw---- 1 root dialout 188, 0  6月  2 10:59 /dev/ttyUSB0

   groups
   cat root dialout sudo audio video realtime gpio

   sudo bash -c 'stty -F /dev/ttyUSB0 9600 cs8 -cstopb -parenb raw -echo -ixon -ixoff; printf "\x01\x03\x00\x00\x00\x0A\xC5\xCD" > /dev/ttyUSB0; timeout 1 cat /dev/ttyUSB0 | xxd -g 1'
   00000000: 01 03 14 00 00 00 3b 01 f4 05 dc 0b b8 00 01 00  ......;.........
   00000010: 3b 01 02 06 00 03 5a 38 ad                       ;.....Z8.
   ```

2. 热成像相机：

   [192.168.2.64](http://192.168.2.64/)
