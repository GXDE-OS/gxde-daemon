#!/usr/bin/env python3
import os
import shutil
import pydbus 
import subprocess
import gi.repository

homePath = os.getenv("HOME")

def ChrootPath():
    return subprocess.getoutput("readlink -f /proc/self/root")

loop = gi.repository.GLib.MainLoop()

class SystemInfo(object):
    """    
        <node>
            <interface name='com.gxde.daemon.system.info'>
                <method name='IsInChroot'>
                    <arg type='b' name='action' direction='out'/>
                </method>
            </interface>
        </node>
    """

    def IsInChroot(self):
        return ChrootPath() != "/"

class SystemUpdateInfo(object):
    """    
        <node>
            <interface name='com.gxde.daemon.system.update'>
                <method name='IsDisabledUpgradeNotifications'>
                    <arg type='b' name='action' direction='out'/>
                </method>
                <method name='DisabledUpgradeNotifications'>
                    <arg type='b' name='action' direction='in'/>
                </method>
            </interface>
        </node>
    """
    notifierStatePath = f"{homePath}/.config/GXDE/disable-gxde-update-notifier"

    def IsDisabledUpgradeNotifications(self):
        return os.path.exists(self.notifierStatePath)
    
    def DisabledUpgradeNotifications(self, option):
        if (option):
            # 禁用更新提醒
            if (not os.path.exists(self.notifierStatePath)):
                os.mknod(self.notifierStatePath)
            return
        # 开启更新提醒
        if (os.path.exists(self.notifierStatePath)):
            os.remove(self.notifierStatePath)

    


bus = pydbus.SessionBus()
bus.publish("com.gxde.daemon.system.info", SystemInfo())
bus.publish("com.gxde.daemon.system.update", SystemUpdateInfo())
loop.run()