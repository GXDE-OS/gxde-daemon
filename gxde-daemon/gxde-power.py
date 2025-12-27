#!/usr/bin/env python3
import gi.repository
import pydbus 
import os

loop = gi.repository.GLib.MainLoop()

def SetCPUGovernor(governor):
	# 启用该 systemd
	os.system("systemctl enable com.gxde.daemon.power")
	for i in os.listdir("/sys/devices/system/cpu/"):
		path = f"/sys/devices/system/cpu/{i}/cpufreq/scaling_governor"
		if (not os.path.exists(path)):
			continue
		with open(path, "w") as file:
			file.write(governor)

class CPU(object):
	"""	
		<node>
			<interface name='com.gxde.daemon.power.cpu'>
				<method name='GetGovernorList'>
					<arg type='as' name='action' direction='out'/>
				</method>
				<method name='Governor'>
					<arg type='s' name='action' direction='out'/>
				</method>
				<method name='SetGovernor'>
					<arg type='s' name='action' direction='in'/>
				</method>
			</interface>
		</node>
	"""
	def GetGovernorList(self):
		path = "/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors"
		if (not os.path.exists(path)):
			return []
		with open(path, "r") as file:
			data = file.read()
		data = data.replace("\n", "").replace("  ", " ")
		return data.split(" ")
		
	def Governor(self):
		if (not os.path.exists("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor")):
			return "powersave"
		with open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r") as file:
			governor = file.read()
		return governor.replace("\n", "")
	
	def SetGovernor(self, governor: str):
		if (not os.path.exists("/etc/GXDE/com.gxde.daemon.power")):
			os.makedirs("/etc/GXDE/com.gxde.daemon.power")
		with open("/etc/GXDE/com.gxde.daemon.power/governor", "w") as file:
			file.write(governor)
		SetCPUGovernor(governor)

# 检测是否以 root 运行
if (os.geteuid() != 0): 
	print("Please run with root!")
	exit(1)
if (os.path.exists("/etc/GXDE/com.gxde.daemon.power/governor")):
	with open("/etc/GXDE/com.gxde.daemon.power/governor", "r") as file:
		SetCPUGovernor(file.read().replace("\n", ""))


bus = pydbus.SystemBus()
bus.publish("com.gxde.daemon.power", CPU(), ("cpu", CPU()))
loop.run()