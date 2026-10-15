# 扬声器

大气环境报警使用 `alarm_category: atmospheric`，音频参数为
`atmospheric_audio`（默认 `atmospheric.wav`），播放次数沿用 `play_count`。
音频使用用户提供的 `atmospheric1014.wav`，格式为单声道 16 kHz、16-bit PCM WAV，
兼容现有 `aplay` 播放器。文件随包安装，不依赖下载目录的绝对路径。
任务中枢只对已开启检测且被接受的阈值报警请求播放；持续异常使用现有重复抑制机制。
全部待播类别结束后，音频节点自行复位，并发布
`/alarm_aggregator/playback_completed`，由任务中枢停止报警灯。

超高频局部放电报警使用 `alarm_category: ultrahigh`，音频参数为
`ultrahigh_audio`（默认 `ultrahigh.wav`）。包内音频来自用户提供的
`检测到局部放电故障.wav`，为单声道 16 kHz、16-bit PCM WAV，时长约 2.22 秒。
检测已启用且传感器 `alarm_level_valid=true`、`alarm_level=2`，并经任务中枢
接受报警后播放，默认播放 3 遍。预警、通信异常和无效等级不会播放局放故障音频。
持续异常沿用任务中枢重复抑制；播放结束不会将故障视为恢复。
音频请求失败时保留平台告警与报警灯。音频随包安装，不依赖下载目录。

手动验证局放音频：

```bash
ros2 service call /alarm_aggregator_node/set_parameters rcl_interfaces/srv/SetParameters \
"{parameters: [
    {name: 'alarm_category', value: {type: 4, string_value: 'ultrahigh'}},
    {name: 'play', value: {type: 1, bool_value: true}}
]}"
```

该指令仅验证音频播放，不启动检测、报警灯或平台事件上报。实际局放任务
沿用 `/monitor/ultrahigh/status` 和平台事件 `alert.ultrahigh`。

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

每个类别默认播放 3 遍。播放期间同类别请求合并，不同类别排队，
按单遍轮转播放；整个队列结束后自行复位，可接收下一次报警。
`play=false` 仍会立即停止播放并清空所有待播类别。次数由 `play_count` 配置。
参数服务按条目依次处理，调用时须先设置 `alarm_category`，再设置 `play=true`。

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

## 统一视觉算法报警

类别 `visual_algorithm`，参数 `visual_algorithm_audio`，默认 `visual_algorithm.wav`。
音频来自 `/home/zxb/下载/视觉算法检测.mp3`，已转换为单声道 16 kHz、
16-bit PCM WAV（约 2.38 秒）并随包安装，不依赖下载目录。
所有进入任务中枢公共算法结果入口、且 `existException` 为整数 `1` 的有效结果
均适用；算法异常结果上报和报告格式保持现有接口。

仅验证扬声器（默认三遍）：

```bash
ros2 service call /alarm_aggregator_node/set_parameters rcl_interfaces/srv/SetParameters \
"{parameters: [
  {name: 'alarm_category', value: {type: 4, string_value: 'visual_algorithm'}},
  {name: 'play', value: {type: 1, bool_value: true}}
]}"
```

该指令不发起算法检测，也不控制机器人运动或启动报警灯。
