# GXDE Daemon

GXDE 桌面环境的插件化 D-Bus 服务管理器。支持运行时发现插件——外部应用只需安装文件到约定目录即可接入。

## 架构

```
gxde-daemon/
├── plugins/                    # 各插件独立目录（构建时）
├── assets/
│   └── plugins/                # 插件 manifest（安装到 /usr/share/gxde-daemon/plugins/）
├── libexec/gxde-daemon/        # 非插件辅助脚本
└── debian/                     # Debian 打包
```

**安装后**，gxde-daemon-manager 通过扫描以下目录发现插件：

| 扫描路径                           | 内容               |
| ---------------------------------- | ------------------ |
| `/usr/share/gxde-daemon/plugins/`  | 插件 manifest JSON |
| `/usr/libexec/gxde-daemon/<name>/` | 插件可执行文件     |

---

## 目录规范

| 安装路径                             | 用途                              |
| ------------------------------------ | --------------------------------- |
| `/usr/libexec/gxde-daemon/<name>/`   | 插件可执行文件及内部模块          |
| `/usr/share/dbus-1/services/`        | 会话 D-Bus service 文件           |
| `/usr/share/dbus-1/system-services/` | 系统 D-Bus service 文件           |
| `/etc/dbus-1/system.d/`              | D-Bus 安全策略（system bus 插件） |
| `/etc/systemd/system/`               | systemd 单元                      |
| `/usr/share/gxde-daemon/<name>/`     | 插件私有资源（assets）            |
| `/usr/share/gxde-daemon/plugins/`    | 插件 manifest（JSON）             |

---

## D-Bus 命名规范

- 使用 `top.gxde.*` 作为 D-Bus bus name 前缀
- `com.deepin.*` 保持不动（历史兼容）
- 为已有 `com.gxde.daemon.*` 接口提供兼容路径：每个 daemon 同时注册 `top.gxde.*` 和 `com.gxde.daemon.*` 两个 bus name，并同时提供对应的 `.service` 文件

---

## 外部应用如何以插件形式安装

外部应用无需修改 gxde-daemon 源码，只需在 deb 包中安装 **3 个文件** 即可自动被 gxde-daemon 识别：

### 1. D-Bus service 文件

放入 `/usr/share/dbus-1/services/`（会话）或 `/usr/share/dbus-1/system-services/`（系统）

```
[D-BUS Service]
Name=top.gxde.xxx
Exec=/usr/libexec/gxde-daemon/<your-plugin>/<your-executable>
```

D-Bus 会在首次调用该 bus name 时自动启动对应可执行文件。**无需注册、无需重启**。

### 2. 可执行文件

放入 `/usr/libexec/gxde-daemon/<your-plugin>/`

可以是任意语言（Python、C、Shell 等）。启动后在代码中注册 D-Bus 接口：

```python
import pydbus
bus = pydbus.SessionBus()
instance = MyService()
bus.publish("top.gxde.xxx", instance)
loop.run()
```

### 3. 插件 manifest

放入 `/usr/share/gxde-daemon/plugins/<your-plugin>.json`

```json
{
    "name": "my-audio-control",
    "description": "系统音量控制",
    "version": "1.0",
    "bus_names": ["top.gxde.audio"],
    "exec": "/usr/libexec/gxde-daemon/my-audio-control/gxde-audio-control",
    "maintainer": "gxde-control-center"
}
```

安装完成后，通过 D-Bus 即可查询：

```bash
dbus-send --session --dest=top.gxde.daemon.manager \
    --print-reply /top/gxde/daemon/manager \
    top.gxde.daemon.manager.ListPlugins
```

### 完整示例：控制中心添加音量插件

`gxde-control-center` 的 `debian/install`：

```
# D-Bus service 文件
misc/dbus/top.gxde.audio.service    usr/share/dbus-1/services/
# 可执行文件
libexec/gxde-audio-control          usr/libexec/gxde-daemon/audio-control/
# 插件 manifest
misc/plugins/audio-control.json     usr/share/gxde-daemon/plugins/
```

安装后即可通过 `top.gxde.audio` 调用音量接口，`gxde-daemon-manager` 的 `ListPlugins()` 会自动发现。

### 与 gxde-daemon 内置插件的区别

|               | 内置插件（本仓库）           | 外部插件              |
| ------------- | ---------------------------- | --------------------- |
| manifest 来源 | `assets/plugins/` → 打包安装 | 各自 deb 安装到同目录 |
| 可执行文件    | `plugins/<name>/` → 打包安装 | 各自 deb 安装到同目录 |
| service 文件  | 内置                         | 各自提供              |
| 运行时发现    | gxde-daemon-manager 统一扫描 | 同一机制              |

---

## 在 gxde-daemon 仓库内新增插件

1. 在 `plugins/` 下创建 `<plugin-name>/` 目录
2. 编写可执行文件，命名为 `gxde-<plugin>`
3. 编写 D-Bus service 文件
4. 在 `assets/plugins/` 下创建 manifest JSON
5. 在 `debian/install` 中添加安装映射

---

## 插件管理器 D-Bus 接口

`top.gxde.daemon.manager` 提供：

| 方法            | 返回值                     | 说明                     |
| --------------- | -------------------------- | ------------------------ |
| `ListPlugins()` | `a{sv}` → `{name: {info}}` | 列出所有已安装插件及详情 |
| `GetPlugin(s)`  | `a{sv}`                    | 查询单个插件             |
| `Rescan()`      | —                          | 强制重新扫描插件目录     |

---

## 现有插件列表

| 插件              | Bus Name（新）               | Bus Name（兼容）                          | 类型    |
| ----------------- | ---------------------------- | ----------------------------------------- | ------- |
| daemon-manager    | `top.gxde.daemon.manager`    | —                                         | Session |
| ai-speaker        | `top.gxde.ai.speaker`        | `com.gxde.daemon.ai.speaker`              | Session |
| ai-translate      | `top.gxde.ai.translate`      | `com.gxde.daemon.ai.translate`            | Session |
| autostart-manager | `top.gxde.autostart.manager` | `com.gxde.daemon.autostart.manager.slimy` | Session |
| corneredge        | `top.gxde.corneredge`        | `com.gxde.daemon.corneredge`              | Session |
| personalization   | `top.gxde.personalization`   | `com.gxde.daemon.personalization`         | Session |
| system-info       | `top.gxde.system.info`       | `com.gxde.daemon.system.info`             | Session |
| system-update     | `top.gxde.system.update`     | `com.gxde.daemon.system.update`           | Session |
| power             | `top.gxde.power`             | `com.gxde.daemon.power`                   | System  |
| fake-license      | `com.deepin.license`         | —                                         | System  |
| dock              | `top.gxde.dock`              | —   | Session |
