# 扬声器

## 编译

```bash
cd ~/Workspace/task_ws
colcon build --packages-select alarm_manager --symlink-install
```

## 启动

```bash
source ~/Workspace/task_ws/install/setup.zsh
ros2 launch alarm_manager alarm_manager.launch.py
```

## 测试

1. 播放“检测到气体泄漏”：

   ```bash
   ros2 service call /alarm_aggregator_node/set_parameters rcl_interfaces/srv/SetParameters \
   "{parameters: [
       {name: 'alarm_category', value: {type: 4, string_value: 'gas'}},
       {name: 'play', value: {type: 1, bool_value: true}}
   ]}"
   ```

2. 播放“检测到生命体征”：

   ```bash
   ros2 service call /alarm_aggregator_node/set_parameters rcl_interfaces/srv/SetParameters \
   "{parameters: [
       {name: 'alarm_category', value: {type: 4, string_value: 'camera'}},
       {name: 'play', value: {type: 1, bool_value: true}}
   ]}"
   ```

3. 停止：

   ```bash
   ros2 service call /alarm_aggregator_node/set_parameters rcl_interfaces/srv/SetParameters \
   "{parameters: [
       {name: 'play', value: {type: 1, bool_value: false}}
   ]}"
   ```

## 接口

| 名称                                    | 类型                               |
| --------------------------------------- | ---------------------------------- |
| `/alarm_aggregator_node/set_parameters` | `rcl_interfaces/srv/SetParameters` |

## 排错

```bash
aplay -l
**** PLAYBACK 硬體裝置清單 ****
card 0: rockchipdp0 [rockchip-dp0], device 0: rockchip-dp0 spdif-hifi-0 [rockchip-dp0 spdif-hifi-0]
  子设备: 1/1
  子设备 #0: subdevice #0
card 1: rockchiphdmi0 [rockchip-hdmi0], device 0: rockchip-hdmi0 i2s-hifi-0 [rockchip-hdmi0 i2s-hifi-0]
  子设备: 1/1
  子设备 #0: subdevice #0
card 2: rockchipes8388 [rockchip-es8388], device 0: dailink-multicodecs ES8323 HiFi-0 [dailink-multicodecs ES8323 HiFi-0]
  子设备: 1/1
  子设备 #0: subdevice #0

aplay -D plughw:CARD=rockchipes8388,DEV=0 ~/Workspace/task_ws/src/alarm_manager/audio/gas_leak.wav
```
