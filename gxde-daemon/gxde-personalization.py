#!/usr/bin/env python3
import os
import json
import shutil
import pydbus 
import traceback
import subprocess
import gi.repository

homePath = os.getenv("HOME")
configDirPath = f"{homePath}/.local/share/GXDE/gxde-k9/slimy/"
configSourcePath = "/usr/share/gxde-daemon/daemon-k9-conf/slimy/"

# ~/.local/share/deepin/themes/deepin/light/decoration.json
userKwinDecorationJson = {
    "light": f"{homePath}/.local/share/deepin/themes/deepin/light/decoration.json",
    "dark": f"{homePath}/.local/share/deepin/themes/deepin/dark/decoration.json"
}

kwinDecorationJson = {
    "light": "/etc/xdg/deepin-decoration/light/decoration.json",
    "dark": "/etc/xdg/deepin-decoration/dark/decoration.json"
}

for i in userKwinDecorationJson.values():
    dirPath = os.path.dirname(i)
    if (not os.path.exists(dirPath)):
        try:
            os.makedirs(dirPath)
        except:
            traceback.print_exc()

def IsAutoStartConfigExists(name: str):
    return (os.path.exists(f"{homePath}/.config/autostart/{name}.desktop") or 
            os.path.exists(f"{homePath}/.local/share/GXDE/gxde-k9/slimy/{name}.slimy"))

def SetAutoStartConfig(name: str, enable: bool):
    # 删除旧的自启动文件
    if (os.path.exists(f"{homePath}/.config/autostart/{name}.desktop")):
        os.remove(f"{homePath}/.config/autostart/{name}.desktop")
    if (os.path.exists(f"{configDirPath}/{name}.slimy")):
        os.remove(f"{configDirPath}/{name}.slimy")

    if (enable):
        shutil.copy(f"{configSourcePath}/{name}.slimy",
                    f"{configDirPath}/{name}.slimy")
        os.system(f"setsid '{name}' > /dev/null 2>&1 &")
        return
    os.system(f"killall '{name}'")

def ModifyJsonRadiusData(data, radius: int):
    radius = int(radius)
    """递归遍历并修改所有层级的'rounded-corner-radius'值"""
    if isinstance(data, dict):
        for key in list(data.keys()):
            if key == "rounded-corner-radius":
                data[key] = f"{radius},{radius}"
            if key == "shadowRadius":
                data[key] = radius
            if key == "blur":
                data[key] = 4
            if key == "shadowOffset":
                data[key] = f"{radius},{radius}"
            # 递归处理子元素
            ModifyJsonRadiusData(data[key], radius)
    elif isinstance(data, list):
        for item in data:
            ModifyJsonRadiusData(item, radius)
    return data

def KillallDeepinKwin():
    os.system("killall deepin-kwin_x11 -9")

def ReadJson(file_path):
    """读取JSON文件并返回数据"""
    with open(file_path, 'r', encoding='utf-8') as file:
        data = json.load(file)
    return data

def WriteJson(data, file_path):
    """将修改后的数据写入JSON文件"""
    with open(file_path, 'w', encoding='utf-8') as file:
        json.dump(data, file, indent=2, ensure_ascii=False)

loop = gi.repository.GLib.MainLoop()

class AutoStartManager(object):
    """    
        <node>
            <interface name='com.gxde.daemon.personalization'>
                <method name='IsOpenTopPanelGlobalMenu'>
                    <arg type='b' name='action' direction='out'/>
                </method>
               <method name='IsOpenTopPanel'>
                    <arg type='b' name='action' direction='out'/>
                </method>
                <method name='IsOpenBottomPanel'>
                    <arg type='b' name='action' direction='out'/>
                </method>
                <method name='IsOpenOrca'>
                    <arg type='b' name='action' direction='out'/>
                </method>
                <method name='SetTopPanel'>
                    <arg type='b' name='action' direction='in'/>
                </method>
                <method name='SetTopPanelGlobalMenu'>
                    <arg type='b' name='action' direction='in'/>
                </method>
                <method name='SetBottomPanel'>
                    <arg type='b' name='action' direction='in'/>
                </method>
                <method name='SetOrca'>
                    <arg type='b' name='action' direction='in'/>
                </method>
                <method name='StopDeepinKwin'>
                </method>
                <method name='SetRadius'>
                    <arg type='i' name='action' direction='in'/>
                </method>
                <method name='Radius'>
                    <arg type='i' name='action' direction='out'/>
                </method>
                <method name='IsDockUseMacMode'>
                    <arg type='b' name='action' direction='out'/>
                </method>
                <method name='SetDockUseMacMode'>
                    <arg type='b' name='action' direction='in'/>
                </method>
            </interface>
        </node>
    """

    def IsOpenTopPanelGlobalMenu(self):
        return IsAutoStartConfigExists("gxde-globalmenu-service")
    
    def IsOpenTopPanel(self):
        return IsAutoStartConfigExists("gxde-top-panel")
        
    def IsOpenBottomPanel(self):
        return IsAutoStartConfigExists("plank")
    
    def IsOpenOrca(self):
        return IsAutoStartConfigExists("orca")
    
    def SetTopPanel(self, isTopPanel: bool):
        SetAutoStartConfig("gxde-top-panel", isTopPanel)

    def SetTopPanelGlobalMenu(self, value: bool):
        SetAutoStartConfig("gxde-globalmenu-service", value)

    def SetBottomPanel(self, value: bool):
        SetAutoStartConfig("plank", value)

    def SetOrca(self, value: bool):
        SetAutoStartConfig("orca", value)

    def StopDeepinKwin(self):
        KillallDeepinKwin()

    def SetRadius(self, radius: int):
        for i in ["light", "dark"]:
            json = ReadJson(kwinDecorationJson[i])
            json = ModifyJsonRadiusData(json, radius)
            WriteJson(json, userKwinDecorationJson[i])
    
    def Radius(self) -> int:
        if (os.path.exists(userKwinDecorationJson["light"])):
            json = ReadJson(userKwinDecorationJson["light"])
        else:
            json = ReadJson(kwinDecorationJson["light"])
        return int(json["1001"]["rounded-corner-radius"].split(",")[0])
    
    def IsDockUseMacMode(self) -> bool:
        return os.path.exists(f"{homePath}/.config/GXDE/gxde-dock/mac-mode")

    def SetDockUseMacMode(self, option):
        configPath = f"{homePath}/.config/GXDE/gxde-dock/mac-mode"
        if (option):
            # 禁用任务栏插件
            if (os.path.exists(configPath)):
                return
            with open(configPath, "w") as file:
                file.write("1")
            # 重启任务栏
            os.system("killall dde-dock -9")
            os.system(f"setsid 'dde-dock' > /dev/null 2>&1 &")
            return
        # 启用任务栏插件
        if (not os.path.exists(configPath)):
            return
        os.remove(configPath)
        os.system("killall dde-dock -9")
        os.system(f"setsid 'dde-dock' > /dev/null 2>&1 &")


bus = pydbus.SessionBus()
bus.publish("com.gxde.daemon.personalization", AutoStartManager())
loop.run()