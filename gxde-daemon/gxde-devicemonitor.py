#!/usr/bin/python3
import gi.repository
import pydbus
import os
import re

loop = gi.repository.GLib.MainLoop()

# /dev/video* 为摄像头设备，/dev/snd/pcmC*D*c 为 ALSA 采集（录音）设备，
# c/p 后缀分别表示 capture/playback，其余（如 controlC*、hwC*D*、seq、timer）
# 会被混音器等长期持有，不能作为占用依据
CAMERA_PATTERN = re.compile(r"^/dev/video\d+$")
MICROPHONE_PATTERN = re.compile(r"^/dev/snd/pcmC\d+D\d+c$")
DEVICE_PATTERN = (CAMERA_PATTERN, MICROPHONE_PATTERN)


def process_name(pid):
	try:
		with open("/proc/%s/comm" % pid) as file:
			return file.read().strip()
	except (IOError, OSError):
		return ""


# 扫描全部进程的 fd（需要 root 权限，可覆盖所有用户及系统服务进程），
# 链接目标指向 /dev/video* 视为摄像头被占用，指向 /dev/snd/pcmC*D*c 视为麦克风被占用
def read_usage():
	usages = [
		{"in_use": False, "procs": []},
		{"in_use": False, "procs": []},
	]
	for entry in os.listdir("/proc"):
		if not entry.isdigit():
			continue
		fd_dir = "/proc/%s/fd" % entry
		try:
			fds = os.listdir(fd_dir)
		except OSError:
			continue
		for fd in fds:
			try:
				target = os.readlink(os.path.join(fd_dir, fd))
			except OSError:
				continue
			for t in (0, 1):
				if DEVICE_PATTERN[t].match(target):
					usages[t]["in_use"] = True
					name = process_name(entry)
					if name and name not in usages[t]["procs"]:
						usages[t]["procs"].append(name)
	return usages


class DeviceMonitor(object):
	"""
		<node>
			<interface name='com.gxde.daemon.devicemonitor'>
				<method name='GetUsage'>
					<arg type='b' name='cameraInUse' direction='out'/>
					<arg type='b' name='micInUse' direction='out'/>
					<arg type='as' name='cameraProcs' direction='out'/>
					<arg type='as' name='micProcs' direction='out'/>
				</method>
				<signal name='UsageChanged'>
					<arg type='b' name='cameraInUse' direction='out'/>
					<arg type='b' name='micInUse' direction='out'/>
					<arg type='as' name='cameraProcs' direction='out'/>
					<arg type='as' name='micProcs' direction='out'/>
				</signal>
			</interface>
		</node>
	"""
	UsageChanged = pydbus.generic.signal()

	def __init__(self):
		self.usages = read_usage()

	def GetUsage(self):
		return (self.usages[0]["in_use"], self.usages[1]["in_use"],
				self.usages[0]["procs"], self.usages[1]["procs"])

	# 定时扫描，仅在状态变化时广播信号；返回 True 维持定时器
	def Poll(self):
		usages = read_usage()
		if (usages != self.usages):
			self.usages = usages
			self.UsageChanged(self.usages[0]["in_use"], self.usages[1]["in_use"],
							  self.usages[0]["procs"], self.usages[1]["procs"])
		return True


# 检测是否以 root 运行
if (os.geteuid() != 0):
	print("Please run with root!")
	exit(1)

bus = pydbus.SystemBus()
monitor = DeviceMonitor()
bus.publish("com.gxde.daemon.devicemonitor", monitor)
gi.repository.GLib.timeout_add_seconds(1, monitor.Poll)
loop.run()
