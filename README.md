# 报警扬声器

## 编译

```bash
cd ~/Workspace/algor_ws
colcon build --packages-select alarm_manager --symlink-install
```

## 启动

```bash
source ~/Workspace/algor_ws/install/setup.zsh
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
