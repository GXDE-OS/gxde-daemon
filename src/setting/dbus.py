import pydbus
import gi.repository

loop = gi.repository.GLib.MainLoop()

class DTK(object):
    """
        <node>
            <interface name='com.gxde.setting.gxde'>
            </interface>
        </node>
    """

bus = pydbus.SessionBus()
bus.publish("com.gxde.setting", DTK, ("dtk", DTK))
loop.run()