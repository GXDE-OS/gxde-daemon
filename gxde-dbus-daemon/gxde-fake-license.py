#!/usr/bin/env python3
import gi.repository
import pydbus 

loop = gi.repository.GLib.MainLoop()

class UOSLicense(object):
	"""	
		<node>
			<interface name='com.deepin.license.Info'>
				<property name="AuthorizationState" type="i" access="read"/>
			</interface>
		</node>
	"""
	AuthorizationState = 1

bus = pydbus.SystemBus()
bus.publish("com.deepin.license", UOSLicense(), ("Info", UOSLicense()))
loop.run()