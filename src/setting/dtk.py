import os
__import__("../global/path.py")

# 设置紧凑模式
def SetDtkSizeMode(value: bool):
    # https://gitee.com/GXDE-OS/deepin-desktop-base/blob/master/gxde.sh
    # 创建文件 ~/.config/gxde/dtk/SIZEMODE 即可启动紧凑模式（需要注销）
    if (os.path.exists("")):
        pass