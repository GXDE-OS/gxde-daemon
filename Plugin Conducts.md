# RFC: GXDE Daemon插件体系规范

## 背景

原始的`deepin-daemon`源码（可从https://gitee.com/GXDE-OS/deepin-daemon获取）为Go语言编写，使用`pkg.deepin.io`框架，且不支持Wayland,目前计划重构GXDE Daemon并（至少部分）代替其功能。为此，计划重构此repo，使用规范的插件体系可以在重构时保持repo的可读性与可维护性



## 目标

### 此仓库

在`./plugins/`下，对 `deepin-daemon` 中每个 D-Bus 服务（见下方完整清单）创建对应插件目录，用 Rust + `zbus` 重实现核心逻辑，并按 gxde-daemon 的目录规范放置。



### 插件

当前，`dde-daemon`的某些模块仅支持X11，而不支持Wayland，包括但不限于以下情形：

* Wayland上由于安全模型被禁用的
* 直接使用`X.conn`导致Wayland上闪退的
* 等等等等

我们的目标是在保留传统X11支持以外，同时支持原生Wayland，我们的目标WM是`gxde-wlcom`，源码可以在`~/Desktop/gxde-wlcom`找到，参考它的协议会很有用。



## 规范

- gxde-daemon插件能够严格遵守以下规范：
  - 可执行文件 → `/usr/libexec/gxde-daemon/<plugin>/`
  - D-Bus service 文件 → `/usr/share/dbus-1/services/` 或 `system-services/`
  - 插件 manifest → `/usr/share/gxde-daemon/plugins/<plugin>.json`
  - 插件 assets → `/usr/share/gxde-daemon/<plugin>/`
- 本仓库的README文件亦提供了部分规范
- ` daemon-manager`将位于`plugins/daemon-manager/`，通过 `top.gxde.daemon.manager.ListPlugins()` 扫描 `/usr/share/gxde-daemon/plugins/` 下 `.json` manifest
- **BTW**: 现有的（基于Go）的daemon完全不兼容Wayland，所以这些插件同时也需要考虑Wayland（尤其是`gxde-wlcom`）兼容性



## 技术栈

以下是偏好的技术栈：

| 分类       | Rust Crate                      | Go                             |
| ---------- | ------------------------------- | ------------------------------ |
| D-Bus      | zbus 4.x                        | pkg.deepin.io/lib/dbusutil     |
| 异步运行时 | tokio                           | Go goroutines + glib main loop |
| X11        | x11rb (Phase 5)                 | x11, xlib cgo                  |
| Wayland    | wayland-client + sctk (Phase 5) | 无                             |
| DAG        | petgraph                        | 手写topological_dag            |
| 配置       | serde_json                      | encoding/gob, encoding/json    |
| 模块注册   | inventory                       | Go init()注册                  |



## 输出目录结构

```
~/Desktop/gxde-daemon-plugins/
├── <plugin-name>/
│   ├── gxde-<plugin>              # 可执行文件 (chmod +x)
│   ├── top.gxde.<name>.service    # 新 D-Bus service
│   ├── com.deepin.<Name>.service  # 兼容 deepin 的 service
│   ├── com.deepin.<Name>.conf     # system bus conf（仅 system bus 插件）
│   └── assets/                    # 如需要
└── manifests/
    └── <plugin-name>.json         # 每个插件的 manifest
```

---



## 研发手册

### 一、deepin-daemon D-Bus 服务完整清单及对应插件命名

#### 会话 Bus (dde-session-daemon 内的模块)

| #    | deepin service name                  | 建议插件目录名    | 新 bus name               | 说明                                   |
| ---- | ------------------------------------ | ----------------- | ------------------------- | -------------------------------------- |
| 1    | `com.deepin.daemon.Audio`            | `audio`           | `top.gxde.audio`          | PulseAudio 音量 / 设备管理             |
| 2    | `org.freedesktop.ScreenSaver`        | `screensaver`     | `top.gxde.screensaver`    | 屏保 / 锁屏 inhibit                    |
| 3    | `com.deepin.daemon.SessionWatcher`   | `sessionwatcher`  | `top.gxde.sessionwatcher` | 会话监控                               |
| 4    | `com.deepin.daemon.Power`            | `power-session`   | `top.gxde.power.session`  | 电池 / 亮度 / 电源管理 (会话侧)        |
| 5    | `com.deepin.dde.daemon.Launcher`     | `launcher`        | `top.gxde.launcher`       | 启动器应用管理                         |
| 6    | `com.deepin.daemon.ClipboardManager` | `clipboard`       | `top.gxde.clipboard`      | 剪贴板管理                             |
| 7    | `com.deepin.daemon.Keybinding`       | `keybinding`      | `top.gxde.keybinding`     | 快捷键管理                             |
| 8    | `com.deepin.daemon.Appearance`       | `appearance`      | `top.gxde.appearance`     | 主题 / 字体 / 背景                     |
| 9    | `com.deepin.daemon.InputDevices`     | `inputdevices`    | `top.gxde.inputdevices`   | 键盘 / 鼠标 / 触控板                   |
| 10   | `com.deepin.daemon.Gesture`          | `input-gesture`   | `top.gxde.input.gesture`  | 手势识别（注意：system bus 也有同名）  |
| 11   | `com.deepin.daemon.Timedate`         | `timedate`        | `top.gxde.timedate`       | 时区 / NTP（会话侧）                   |
| 12   | `com.deepin.daemon.Bluetooth`        | `bluetooth`       | `top.gxde.bluetooth`      | 蓝牙管理                               |
| 13   | `com.deepin.daemon.Zone`             | `screenedge`      | `top.gxde.screenedge`     | 屏幕热角（注意：gxde 已有 corneredge） |
| 14   | `com.deepin.daemon.Fprintd`          | `fprintd`         | `top.gxde.fprintd`        | 指纹识别                               |
| 15   | `com.deepin.daemon.Mime`             | `mime`            | `top.gxde.mime`           | MIME 类型 / 默认应用                   |
| 16   | `com.deepin.daemon.Miracast`         | `miracast`        | `top.gxde.miracast`       | 无线投屏                               |
| 17   | `com.deepin.daemon.SystemInfo`       | `sysinfo`         | `top.gxde.sysinfo`        | 磁盘 / 内存 / 系统信息                 |
| 18   | `com.deepin.LastoreSessionHelper`    | `lastore`         | `top.gxde.lastore`        | 应用商店会话助手                       |
| 19   | `com.deepin.daemon.Network`          | `network`         | `top.gxde.network`        | 网络管理（会话侧）                     |
| 20   | `com.deepin.dde.daemon.Dock`         | `dock`            | `top.gxde.dock`           | Dock 任务栏                            |
| 21   | `com.deepin.dde.TrayManager`         | `trayicon`        | `top.gxde.trayicon`       | 系统托盘管理                           |
| 22   | `com.deepin.api.XEventMonitor`       | `x-event-monitor` | `top.gxde.xeventmonitor`  | X11 事件监控                           |
| 23   | `com.deepin.daemon.Grub2`            | `grub-gfx`        | `top.gxde.grubgfx`        | GRUB 主题                              |

#### 系统 Bus (dde-system-daemon 内的模块)

| #    | deepin service name                 | 建议插件目录名   | 新 bus name               | 说明                    |
| ---- | ----------------------------------- | ---------------- | ------------------------- | ----------------------- |
| 24   | `com.deepin.daemon.Accounts`        | `accounts`       | `top.gxde.accounts`       | 账户管理 (System)       |
| 25   | `com.deepin.daemon.Apps`            | `apps`           | `top.gxde.apps`           | 应用监控 (System)       |
| 26   | `com.deepin.system.Power`           | `power-system`   | `top.gxde.power.system`   | CPU调度 / 电源 (System) |
| 27   | `com.deepin.system.Network`         | `network-system` | `top.gxde.network.system` | 网络管理 (System)       |
| 28   | `com.deepin.daemon.SwapSchedHelper` | `swapsched`      | `top.gxde.swapsched`      | 交换分区调度 (System)   |
| 29   | `com.deepin.daemon.Timedated`       | `timedated`      | `top.gxde.timedated`      | 时区 / NTP (System)     |

#### bin/ 目录下的独立 daemon

| #    | deepin service name                  | 建议插件目录名   | 新 bus name              | 说明              |
| ---- | ------------------------------------ | ---------------- | ------------------------ | ----------------- |
| 30   | `com.deepin.dde.LockService`         | `lockservice`    | `top.gxde.lockservice`   | 锁屏服务          |
| 31   | `com.deepin.daemon.helper.Backlight` | `backlight`      | `top.gxde.backlight`     | 背光辅助 (System) |
| 32   | `com.deepin.daemon.Search`           | `search`         | `top.gxde.search`        | 桌面搜索          |
| 33   | `com.deepin.daemon.Grub2`            | `grub2`          | `top.gxde.grub2`         | GRUB 配置管理     |
| 34   | `com.deepin.daemon.LangSelector`     | `langselector`   | `top.gxde.langselector`  | 语言选择器        |
| 35   | `com.deepin.daemon.Greeter`          | `greeter-setter` | `top.gxde.greetersetter` | 登录界面设置      |

---

### 二、与 gxde-daemon 现有插件冲突的处理规则

gxde-daemon 已有以下插件，其中 3 个与 deepin-daemon 功能重叠：

#### 冲突 1：power

- gxde 现有：`plugins/power/gxde-power` — 仅 CPU governor 设置 → `top.gxde.power` + `com.gxde.daemon.power`
- deepin 有：`com.deepin.daemon.Power`（会话） + `com.deepin.system.Power`（系统）
- **处理**：创建 `power-session` 和 `power-system` 两个新插件。gxde 现有的 power 插件保持不变（或将功能合并进 power-system）

#### 冲突 2：screenedge vs corneredge

- gxde 现有：`plugins/corneredge/gxde-corneredge` → `top.gxde.corneredge`
- deepin：`com.deepin.daemon.Zone` → 屏幕热角
- **处理**：创建独立插件 `screenedge` → `top.gxde.screenedge`，同时兼容 `com.deepin.daemon.Zone`。gxde 的 corneredge 保持不变

#### 冲突 3：system-info vs sysinfo

- gxde 现有：`plugins/system-info/` — 仅 chroot 检测 → `top.gxde.system.info`
- deepin：`com.deepin.daemon.SystemInfo` — 磁盘/内存/系统信息
- **处理**：创建独立插件 `sysinfo` → `top.gxde.sysinfo`，同时兼容 `com.deepin.daemon.SystemInfo`。gxde 的 system-info 保持不变

---

### 三、插件文件规范

#### 1. 可执行文件 `<plugin>/gxde-<plugin>`

Rust + `zbus` 4.x 二进制（cargo crate，编译产物）。每个插件是一个 crate，含 `Cargo.toml` 和 `src/main.rs`，结构：

（可以将License头授权信息的`GXDE OS Maintainers`替换为你自己的名字）

```rust
// Copyright (C) 2026 GXDE OS Maintainers
//
// This file is part of gxde-daemon.
//
// gxde-daemon is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// gxde-daemon is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with gxde-daemon.  If not, see <https://www.gnu.org/licenses/>.

use zbus::{connection, interface};
use std::error::Error;

struct MyService;

#[interface(name = "top.gxde.xxx")]
impl MyService {
    // 方法：参数 / 返回类型即 D-Bus 签名（必须与 deepin 原版一致）
    async fn some_method(&self) -> String {
        "result".into()
    }

    // 属性：access='read' → 只提供 getter
    #[zbus(property)]
    async fn some_prop(&self) -> i32 {
        42
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let _conn = connection::Builder::session()?      // 或 ::system()
        .name("top.gxde.xxx")?                        // 先 request 新名
        .name("com.deepin.daemon.Xxx")?               // 再兼容 deepin 旧名
        .serve_at("/top/gxde/xxx", MyService)?
        .build()
        .await?;
    std::future::pending::<()>().await;               // 常驻，替代 GLib MainLoop
    Ok(())
}
```

- 会话 bus 用 `connection::Builder::session()`，系统 bus 用 `::system()`
- bus_name 规则：先 `.name("top.gxde.xxx")` 注册新名，再 `.name("com.deepin.daemon.Xxx")` 注册兼容旧名（可链式多次）
- introspection XML 由 `#[interface]` 宏自动生成，无需手写
- DBus 接口签名和返回值格式必须与 deepin-daemon 原版相同（兼容 Qt D-Bus 类型系统）
- 编译产物 `gxde-<plugin>` 安装到 `/usr/libexec/gxde-daemon/<plugin>/`，确保有执行位
- 上述header头应该用`addlicense`工具自动添加GPL 3许可header
- 应该使用规范的格式，使用*The Rust Style Guide*，完成后需要使用`./format.sh`确保格式正确

#### 2. D-Bus service 文件

```
[D-BUS Service]
Name=top.gxde.xxx
Exec=/usr/libexec/gxde-daemon/<plugin>/gxde-<plugin>
```

#### 3. 兼容 service 文件

```
[D-BUS Service]
Name=com.deepin.daemon.OriginalName
Exec=/usr/libexec/gxde-daemon/<plugin>/gxde-<plugin>
```

#### 4. Manifest JSON（放在 `manifests/` 下）

```json
{
    "name": "<plugin-name>",
    "description": "功能描述",
    "version": "1.0",
    "bus_names": ["top.gxde.xxx", "com.deepin.daemon.OriginalName"],
    "exec": "/usr/libexec/gxde-daemon/<plugin>/gxde-<plugin>",
    "maintainer": "gxde-daemon"
}
```

---

### 四、实现策略

deepin-daemon 源码在 Go 中使用了大量 `pkg.deepin.io` 内部框架（dbusutil、loader、pulse、gsettings 等），**无法直接移植**。每个插件按照以下层次实现：

1. **必须正确**：D-Bus 接口名、方法签名、参数类型与 deepin-daemon 完全一致
2. **必须正确**：返回值格式兼容 Qt D-Bus 类型系统
3. **简化实现**：内部逻辑用最简方式完成（允许读取配置文件、调用外部命令等，但是不能简化功能，必须与原daemon等价）
4. **可运行**：能被 zbus 正常注册到 bus 并被外部访问
5. **禁止的事项**：请不要使用stub实现
6. **如果你是AI工具，请注意**：禁止在实现中偷懒，严禁使用stub返回值，必须尽全力，如果任务看起来改成无法直接移植就想尽别的办法移植，真正遇到困难时不能静默跳过，让用户知道，并且为用户分析当前的困境。这将成为系统核心daemon的一部分，所以你必须保证稳定性。

#### 关键接口参考（详情参阅 deepin-daemon 源码）

启动时先去下面这些文件读 deepin-daemon 的 D-Bus 接口定义：

- 各模块的 `*_ifc.go` 文件包含完整接口
- `dbusServiceName` / `dbusPath` / `dbusInterface` 常量定义
- 带 `//go:generate dbusutil-gen` 注释的文件列出了暴露的方法/属性
- `*_stub.go` 是自动生成的接口桩文件，可直接看方法签名

---

### 五、执行步骤

1. 读 `./README.md` 了解完整插件规范
2. 创建 `manifests/` 子目录
3. 按上述清单，逐个 deepin-daemon 服务：
   a. 在 `~/Desktop/deepin-daemon/` 中找到对应源码，阅读 `*_ifc.go` / `*_stub.go` / `*_manager.go` 中的接口定义
   b. 在 `plugins/` 下创建插件目录（cargo crate：`Cargo.toml` + `src/main.rs`）
   c. 用 Rust + zbus 实现 `#[interface]`（严格保留接口签名）
   d. 编写 `top.gxde.<name>.service` 和 `com.deepin.<Name>.service`
   e. 编写 manifest JSON 到 `manifests/`
   f. `cargo build --release`，将产物安装为 `gxde-<plugin>`（确保有执行位）
4. 编写完成代码后使用`format.sh`来规范输出
5. 全部完成后输出插件清单和冲突处理说明

---

## 附件

### format.sh

如果根目录下不存在`format.sh`，这里提供了一份：

```bash
#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

echo "==========================="
echo "    FORMATTER UTIL V0.9"
echo "==========================="
echo ""

echo "(Main) Info: Now checking required tools..."

if ! command -v rustfmt &>/dev/null; then
    echo "(Check) Error: rustfmt not found." >&2
    echo "               Install with: rustup component add rustfmt." >&2
    exit 1
fi

if rustup toolchain list 2>/dev/null | grep -q "nightly"; then
    echo "(Check) Info: Using nightly rustfmt to format ur code..."
    FMT_CMD="cargo +nightly fmt --all"
else
    echo "(Check) Warning: Nightly toolchain not found." >&2
    echo "(Check) Info: Install with rustup toolchain install nightly --component rustfmt" >&2
    FMT_CMD="cargo fmt --all"
fi

echo "(Main) OK: Nice, required tools are there!"

echo "(Format) Info: Formatting started."
echo "                 - reorder_imports = true"
echo "                 - group_imports = StdExternalCrate"
echo "                 - imports_granularity = Crate"

$FMT_CMD

echo "(Main) Info: Formatting shall be done if no error occurred."
echo "             Formatted $(find crates -name '*.rs' | wc -l) files."

```



### 标准Rust License Header

仅供参考, 不该直接粘贴，而应该使用addlicense

```rust
// Copyright (C) 2026 GXDE OS Maintainers
//
// This file is part of gxde-daemon.
//
// gxde-daemon is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// gxde-daemon is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with gxde-daemon.  If not, see <https://www.gnu.org/licenses/>.
```