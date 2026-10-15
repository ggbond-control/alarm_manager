"""Exercise the real alarm node with a silent mock player in an isolated ROS domain."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time
import unittest
import wave

os.environ["ROS_DOMAIN_ID"] = "175"
os.environ["ROS_AUTOMATIC_DISCOVERY_RANGE"] = "LOCALHOST"
os.environ["RMW_IMPLEMENTATION"] = "rmw_fastrtps_cpp"

import rclpy
from rcl_interfaces.msg import Parameter, ParameterType, ParameterValue
from rcl_interfaces.srv import SetParameters
from std_msgs.msg import Empty


class AlarmCategoryAudioTest(unittest.TestCase):
    def test_category_playback_duplicate_stop_and_replay(self):
        executable, audio_path = sys.argv[1:3]
        category = sys.argv[3] if len(sys.argv) > 3 else "atmospheric"
        with wave.open(audio_path) as audio:
            self.assertEqual(audio.getsampwidth(), 2)
            self.assertEqual(audio.getnchannels(), 1)
            self.assertGreater(audio.getnframes(), 0)
        with tempfile.TemporaryDirectory(prefix=f"{category}_audio_") as directory:
            directory = Path(directory)
            plays = directory / "plays.txt"
            player = directory / "mock_player"
            player.write_text('#!/usr/bin/python3\nimport sys,time\n'
                              f'with open({str(plays)!r}, "a") as f: f.write(sys.argv[1]+"\\n")\n'
                              'time.sleep(0.35)\n')
            player.chmod(0o755)
            rclpy.init()
            node = rclpy.create_node(f"{category}_audio_test")
            completions = []
            subscription = node.create_subscription(
                Empty, "/alarm_aggregator/playback_completed", lambda msg: completions.append(msg), 10)
            with (directory / "node.log").open("w") as log:
                process = subprocess.Popen([
                    executable, "--ros-args", "-p", f"audio_base_dir:={Path(audio_path).parent}",
                    "-p", f"player_command:={player}", "-p", 'fallback_player_command:=""',
                    "-p", "play_count:=3"], stdout=log, stderr=subprocess.STDOUT)
                try:
                    client = node.create_client(SetParameters, "/alarm_aggregator_node/set_parameters")
                    self.assertTrue(client.wait_for_service(timeout_sec=15))

                    def send(**parameters):
                        request = SetParameters.Request()
                        for name, value in parameters.items():
                            param_value = ParameterValue()
                            if isinstance(value, bool):
                                param_value.type = ParameterType.PARAMETER_BOOL
                                param_value.bool_value = value
                            else:
                                param_value.type = ParameterType.PARAMETER_STRING
                                param_value.string_value = value
                            request.parameters.append(Parameter(name=name, value=param_value))
                        future = client.call_async(request)
                        rclpy.spin_until_future_complete(node, future, timeout_sec=5)
                        self.assertTrue(future.done())
                        return future.result().results

                    def spin_until(condition, timeout=5):
                        deadline = time.monotonic() + timeout
                        while time.monotonic() < deadline and not condition():
                            rclpy.spin_once(node, timeout_sec=.05)
                        self.assertTrue(condition())

                    self.assertTrue(all(r.successful for r in send(
                        alarm_category=category, play=True)))
                    # Repeated requests while latched must not enqueue another sequence.
                    self.assertTrue(all(r.successful for r in send(
                        alarm_category=category, play=True)))
                    spin_until(lambda: len(completions) == 1)
                    self.assertEqual(plays.read_text().splitlines(), [audio_path] * 3)
                    self.assertTrue(all(r.successful for r in send(play=False)))
                    self.assertTrue(all(r.successful for r in send(
                        alarm_category=category, play=True)))
                    spin_until(lambda: len(completions) == 2)
                    self.assertEqual(plays.read_text().splitlines(), [audio_path] * 6)
                    self.assertTrue(all(r.successful for r in send(play=False)))
                    failed = send(**{f"{category}_audio": "missing.wav", "alarm_category": category, "play": True})
                    self.assertTrue(any(not r.successful for r in failed))
                    self.assertTrue(any("未找到报警音频文件" in r.reason for r in failed))
                    self.assertEqual(len(plays.read_text().splitlines()), 6)
                    if category == "visual_algorithm":
                        self.assertTrue(all(r.successful for r in send(
                            visual_algorithm_audio=Path(audio_path).name)))
                        # Mixed categories must each finish three plays, with one drain notification.
                        start = len(plays.read_text().splitlines())
                        for kind in ("atmospheric", "visual_algorithm", "ultrahigh", "visual_algorithm"):
                            self.assertTrue(all(r.successful for r in send(alarm_category=kind, play=True)))
                        spin_until(lambda: len(completions) == 3, timeout=8)
                        mixed = plays.read_text().splitlines()[start:]
                        self.assertEqual(len(mixed), 9)
                        for kind in ("atmospheric", "visual_algorithm", "ultrahigh"):
                            self.assertEqual(mixed.count(str(Path(audio_path).parent / f"{kind}.wav")), 3)
                        # Self-reset accepts a new batch without a global play=false round trip.
                        self.assertTrue(all(r.successful for r in send(
                            alarm_category=category, play=True)))
                        spin_until(lambda: len(completions) == 4)
                        # Stop during launch also cancels queued categories.
                        for kind in ("visual_algorithm", "ultrahigh"):
                            send(alarm_category=kind, play=True)
                        send(play=False)
                        time.sleep(.5)
                        stopped_count = len(plays.read_text().splitlines())
                        until = time.monotonic() + 1
                        while time.monotonic() < until:
                            rclpy.spin_once(node, timeout_sec=.05)
                        self.assertEqual(len(plays.read_text().splitlines()), stopped_count)
                        self.assertEqual(len(completions), 4)
                except BaseException:
                    log.flush()
                    print((directory / "node.log").read_text(), file=sys.stderr)
                    raise
                finally:
                    process.send_signal(signal.SIGINT)
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait()
                    node.destroy_subscription(subscription)
                    node.destroy_node()
                    rclpy.shutdown()


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]])
