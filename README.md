# MU Online 客户端源码

[![MinGW Build](https://github.com/sven-n/MuMain/actions/workflows/mingw-build.yml/badge.svg?branch=main)](https://github.com/sven-n/MuMain/actions/workflows/mingw-build.yml)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/sven-n/MuMain)

> ## ⚠️ 本仓库为**简体中文（zh-CN）汉化项目**
>
> 本分支在官方 `main` 分支基础上做了**简体中文汉化**：游戏内 UI 文本、物品/技能/怪物/NPC 名称均已本地化为中文。
> 汉化内容由独立的 `localization` 维护仓库统一维护，可随官方更新重放，不改动原有玩法。
>
> - **汉化明细与维护入口**：见下方「简体中文汉化项目」一节。
> - **运行汉化**：`config.ini` 需设置 `[LOGIN] Language=zh-CN`（本地数据包装载）与 `[UI] Locale=zh-CN`（resx UI 文本），两者都要设。
> - 除下方「本分支相对官方上游的改动」列出的内容（挂机增强、崩溃修复、诊断与汉化配套改动）外，本分支功能与官方上游一致。

---

## 本仓库的定位：整套环境的**原生前端**

本仓库是一个 MU Online 私服开发环境里的**客户端（前端）**——一个用 C++20 + OpenGL 3.3 写的
原生桌面客户端，负责渲染 3D 世界、角色动画、UI 与音效，并接收键鼠输入。

它**自己不含任何游戏规则**：伤害怎么算、怪掉什么、经验给多少，全部由服务端决定。
客户端只做两件事——把玩家操作发上去，把服务器下发的状态画出来。所以**必须先有一台服务器在跑**，
本仓库配套的服务端是 [OpenMU](https://github.com/MUnique/OpenMU)（本环境用的汉化分支：`OpenMU-CN`）。

同一台 OpenMU 服务器同时接三种前端，三者**共用一套账号、一套数据库、同一个世界**
——用网页端练的角色，用本客户端登进去还是同一个：

```
  ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓
  ┃ ① MuMain（★ 本仓库）        ┃  C++20 / OpenGL 3.3 / SDL3
  ┃   原生桌面客户端，端口 44406 ┃  网络栈 = .NET 10 Native AOT 库
  ┗━━━━━━━━━━━━┳━━━━━━━━━━━━━━━┛
               │
  ┌────────────┴───────────────┐
  │ ② 网页端（MUWebClient）     │  浏览器 Phaser 4 + Vue 3
  │   浏览器 ──WS──▶ .NET 代理  │  代理把 WS JSON 翻成原生封包
  └────────────┬───────────────┘
               │      原生 MU TCP 协议（S6E3）+ SimpleModulus/Xor32 加密
  ┌────────────┴───────────────┐
  │ ③ 原版 Webzen 客户端        │  端口 44405
  └────────────┬───────────────┘
               ▼
      ┌──────────────────────┐
      │  OpenMU 游戏服务器    │  C# / .NET 10
      └──────────┬───────────┘
                 ▼
            PostgreSQL
```

### 本客户端与网页端是什么关系

两者是**并列的两个前端**，互不依赖、互不通信，各自直连服务器：

| | MuMain（本仓库） | 网页端（MUWebClient） |
|---|---|---|
| 运行环境 | Windows 桌面 `.exe` | 任意浏览器 |
| 画面 | 原版素材 3D，OpenGL | 2D 俯视/横版，Phaser 4 Canvas |
| 连接 | **直连** TCP `44406` | 浏览器 WS → **.NET 代理** → TCP `55901` |
| 定位 | 完整游戏体验 | 轻量挂机 / 随时随地看进度 |

> 浏览器开不了裸 TCP、也做不了 MU 的封包加密，所以网页端必须多一层代理进程；
> 而本客户端自带 .NET 网络库，直接说原生协议，不需要代理。
>
> ⚠️ **本仓库的协议是"扩展过的"，不是标准 S6E3**（伤害/经验突破 16 位、物品与外观序列化改过、
> 攻击后多了怪物血条）。所以它**只能连 OpenMU**，连不了原版商业服务端；反过来原版客户端
> 也用不了这些扩展。三个前端能共存，是因为 OpenMU 对每种连接分别按对应协议编解码。

---

## 快速启动

前提：**OpenMU 服务器已经在跑**，且本仓库已编译出 `Main.exe`。

### 最快：用仓库内的启动脚本

```bat
start-mumain.bat
```

脚本跑的是 `out\build\windows-x86\src\Main.exe connect /u127.127.127.127 /p44406`。
若还没编译，脚本会提示先执行 `cmake --preset windows-x86 && cmake --build --preset windows-x86-release`。

### 手动启动

```bat
Main.exe connect /u<服务器IP> /p44406
```

不带参数时默认连 `localhost:44406`。完整的构建方式（五种 IDE / 命令行路径）、
`config.ini` 全部配置项与命令行开关见下方「如何构建与运行」一节。

---

## 本分支相对官方上游的改动

除简体中文汉化外，本分支还带了以下改动（均已在源码处批注说明原因）：

### 1. 挂机增强：跑城事务 + 推荐挂机点

* **`src/source/MUHelper/TownRun.{h,cpp}`（新增）** —— 给在线 MU Helper 加了"跑城"状态机：
  药水见底 / 背包淤满 / 精灵士兵 buff 过期时，自动传送回城 → 走到对应 NPC（仓库管理员 / 药水商人 /
  精灵士兵）→ 完成交互 → 传回原挂机点继续打。跑城期间跳过战斗与拾取流程，并绕过安全区停手保护。
* **`src/source/UI/NewUI/HUD/NewUIAfkSpotWindow.{h,cpp}`（新增）** —— "当前地图推荐挂机点"窗口，
  点一行即执行"领 buff + 补药 → 传送 → 走到目标格 → 开始挂机"。
* **`src/source/MUHelper/AfkSpotsData.h`（新增，自动生成）** —— 挂机点数据，由
  `tools/gen_afk_spots_native.py` 从数据库的 `config.MonsterSpawnArea` + 地形数据生成。
  ⚠️ 这是**生成产物，不要手改**；它按**原生客户端**的找怪半径（`iHuntingRange=6`，欧氏 R=9）计算，
  与网页端离线幽灵用的那份（R=15）是**两套不同的数据**。

配套的协议与 UI 改动：

* **`Network/Server/WSclient.h`** —— MU Helper 设置封包 Index 33 原本有 4 个保留位，
  现在用掉了：`bPickMagicItems`（拾取蓝装）/ `bAutoNpcBuff` / `bAutoBuyPotions` / `bAutoStoreVault`。
  ⚠️ 这四位与 OpenMU 侧的 `IMuHelperSettings` 是**一一对应的约定**，改一边必须改另一边。
* **`Engine/Object/ZzzInventory.{h,cpp}`** —— 新增 `IsMagicItem()`，判定规则与物品 tooltip 里
  蓝色名字的逻辑一致（带技能 / 幸运 / 追加选项，且既非卓越也非古代）。
* **`UI/NewUI/NPCs/NewUINPCShop.{h,cpp}`、`UI/NewUI/Inventory/NewUIStorageInventory.cpp`** ——
  暴露内部 inventory 控件，供跑城状态机自动完成买药 / 存货。

### 2. 崩溃修复与诊断增强

* **NPC 对话框崩溃**（`UI/NewUI/NPCs/NewUINPCDialogue.cpp`）——
  `CNewUINPCDialogue` 的分页字段从来没被初始化。当 `SetCurNPCWords()` 提前返回（例如某个 NPC 的
  对话文本解析为空）时，这些字段保持垃圾值（Debug 下是 `0xCCCCCCCC`），随后 `RenderText()` 拿它们
  去索引静态数组，越界成野指针 → 访问违例。修法：构造器里逐个清零，并让"空对话"安全降级成一页空白
  而不是崩溃。顺带把 `SetQuestListText()` 的数量钳位从 `_ASSERT`（Release 下会被编译掉）改成真钳位，
  避免畸形/超长服务器封包溢出数组。
* **退出时崩溃**（`Core/Time/FrameTimerScheduler.cpp`）——
  单例原本是函数内 `static` 对象，但有些子系统在**静态析构期**才调 `Kill()`（如 `CNewUISystem` 拆除时
  跑的 `CSlideHelpMgr` 析构），此时该单例可能已被销毁 → use-after-free。改为堆分配且**永不销毁**，
  用进程退出时泄漏一个极小对象换取"调度器一定活得比引用它的子系统久"。

* **网络封包处理的两处越界/空指针**（`Network/Server/WSclient.cpp`）——
  战盟成员视窗包里 `FindCharacterIndex` 找不到活人时返回越界哨兵值，原来照写会越界，现在跳过；
  NPC 血条更新对空指针解引用，现在加判空。

* **进图失败的静默退出留痕**（`Render/Terrain/ZzzLodTerrain.cpp`、`Render/Textures/ZzzTexture.cpp`）——
  地形属性校验失败 / 贴图加载失败时原本直接弹框后 `ExitProcess(0)`，`MuError.log` 里什么都不剩，
  看上去就是"进图闪退"。现在弹框前先把错误写进日志。
* **调试浮层默认关闭**（`Scenes/SceneManager.cpp`）——左上角 FPS/调试信息原本 Debug 构建默认开，
  现在一律默认关，需要时在聊天框发 `$details on/off` 运行时切换。

### 3. 与汉化配套的小改动

* **`Core/Globals/_define.h`** —— `MAX_LANGUAGE_NAME_LENGTH` 4 → 16，否则语言名（如 `zh-CN`）
  在协议里会被截断。
* **`bin/config.ini.template`** —— 模板默认语言由 `Eng` 改为 `zh-CN`。
* **NPC 头顶台词去重**（`UI/Chat/Chat.cpp` `CreateChat`）——加 `wcscmp` 判重，避免同一 NPC
  连续刷同一句台词叠成一片。这是汉化引入的**唯一**源码改动，上游更新覆盖后需重打。

### 4. 编译提示

Windows 下命令行编译必须在 **"x86 Native Tools Command Prompt for VS 2022"** 里执行，
否则找不到 MSVC 编译器：

```bat
cmake -S . -B out\build\windows-x86 -G Ninja -DENABLE_EDITOR=OFF -DCMAKE_CXX_FLAGS="/utf-8"
cmake --build out\build\windows-x86
```

> 判断是否真的编译成功，请看 **`Main.exe` 的时间戳**，不要只看日志末尾。

---

这是 [Louis 上传的 Season 5.2 客户端源码](https://github.com/LouisEmulator/Main5.2) 的一个特别分支。

长期目标是清理这份源码，并使其兼容且功能完整地达到 **Season 6 Episode 3**。

到目前为止已完成的内容：
  * 🔥 帧率提升。
    * 默认使用 V-Sync 且不限制 FPS；如果设备不支持 V-Sync，则限制为 60 FPS。
    * 选项菜单新增复选框，可以关闭特效以获得更高帧率。
    * 聊天命令：
      * 修改 FPS 上限：`$fps <value>`
      * V-Sync 开关：`$vsync on` / `$vsync off`
      * 显示简易 FPS 计数器：`$fpscounter on` / `$fpscounter off`
      * 显示详细性能面板（FPS 统计、百分位、帧图）：`$details on` / `$details off`
      * 显示 GL 调用/绘制/缓冲计数器以及每个 render pass 的 GPU 计时：`$glstats on` / `$glstats off`
  * 🔥 通过改用顶点数组 (vertex arrays) 优化了部分 OpenGL 调用。这在大量玩家与物体可见时，应该会带来更好的帧率。
  * 🔥 Core Profile GL 性能系列（见 [docs/GPU Skinning/glperf](docs/GPU%20Skinning/glperf/README.md)）：
    用环形缓冲 UBO 流式传输取代每次更新时丢弃/重传缓冲，通过纹理对分桶把地形绘制调用压缩到原来的约 1/25，并移除了每帧冗余的 GL 状态切换。开发机上测得的净收益：平均 FPS +4.4%、1% Low +28.0%、帧时间 -4.1%。
  * 🔥 新增背包与仓库扩展。
  * 🔥 大师技能树系统升级到 Season 6 版本。
  * 🔥 Unicode 支持：客户端内存中改用 UTF-16LE 而不是 ANSI。所有字符串与字符数组都改为宽字符。
    来自文件与网络的字符串按 UTF-8 处理。
  * 🔥 用 MUnique.OpenMU.Network 替换了整个网络栈，使后续改动更容易。本仓库附带一个使用
    Native AOT 编译的 C# .NET 10 客户端库。
  * 🔥 网络协议已适配 Season 6 Episode 3 —— 可能还有部分工作要做，但已经能连接
    [OpenMU](https://github.com/MUnique/OpenMU) 并且可以游玩。另外协议也做了扩展，已不是标准协议。
    * 伤害、经验等现在可以超过 16 位。
    * 改进的物品序列化。
    * 改进的外观 (appearance) 序列化。
    * 攻击后新增怪物血条。
  * 🔥 吸收了 Qubit 的大量改动，例如：
    * 烈斗士 (Rage Fighter) 职业
    * 黑暗领主 (Dark Lord) 与渡鸦 (Raven) 并行行走时的视觉 bug
    * 鼠标右键装备物品
    * 红色、蓝色与黑色芬里尔 (Fenrir) 的发光特效
    * 额外的屏幕分辨率
  * 🔥 内置了 MU Helper 界面与逻辑 —— 还有部分工作要做，但核心功能可用。
  * 🔥 自动重连系统。
  * 目标版本已经是 Season 6，因此移除了烈斗士相关的 if-def 条件——烈斗士应始终被包含在内。
  * 一些小型 bug 修复，例如：
    * Storm Crow 物品标签
    * Ancient 套装标签
  * 代码已重构。大量魔法数值被替换为枚举与常量。
  * 🔥 新的翻译系统（见 [docs/translation-system.md](docs/translation-system.md)）

Season 6 剩余工作：
  * Lucky Items

## 如何构建与运行

### 环境要求
* **CMake** 3.25 或更新（Visual Studio 与 CLion 自带）
* **.NET SDK 10.0** 或更新（用于构建 Client Library）
* **Visual Studio 2022+**（需安装 C++ 与 C# 工作负载）、**CLion** 或 **Rider**（见下方各 IDE 专属说明）
* 可兼容的服务器：[OpenMU](https://github.com/MUnique/OpenMU)

### 首次配置 —— 初始化子模块

项目在 `src/ThirdParty/` 下使用三个 git 子模块：

- `SDL` —— 窗口、输入与音频后端（所有构建必需）
- `SDL_mixer` —— 音频混音器（所有构建必需）
- `imgui` —— 游戏内编辑器 UI（仅当以 `-DENABLE_EDITOR=ON` 构建时所需，与 Debug/Release 无关）

CMake 会在首次 configure 时自动初始化这些子模块。如果失败，在仓库根目录运行：

```bash
git submodule update --init
```

### 构建配置

有两个相互独立的维度：**编辑器开/关**（configure 时决定，通过 preset 选择）与 **Debug/Release**（构建时决定）。

#### 编辑器构建（`windows-x86-mueditor` / `windows-x64-mueditor`）
- configure preset 设 `ENABLE_EDITOR=ON`
- 包含基于 ImGui 的游戏内 MU Editor；`imgui` 子模块必须已初始化
- 游戏中按 **F12** 切换编辑器
- 以 `--editor` 参数启动可让启动时就启用编辑器
- 预处理宏：`_EDITOR`

#### 标准构建（`windows-x86` / `windows-x64`）
- configure preset 设 `ENABLE_EDITOR=OFF`；不编译任何编辑器代码，`imgui` 子模块也不初始化
- 编辑器相关开销为零

两种配置都可以通过对应的 build preset（`*-debug` 或 `*-release`）按 Debug 或 Release 构建。

### 使用 CMake 与 MinGW-w64 构建（Linux）

仓库也包含了一套 CMake 配置，可以跨平台编译 Windows 客户端。

**前置条件**

  * 可用的 MinGW-w64 工具链（例如 `i686-w64-mingw32-g++`）。
  * 一份提供 `libturbojpeg` 库的 MinGW-w64 libjpeg-turbo 构建（静态库或导入库均可），并放在工具链的库搜索路径上。
  * 随 MinGW-w64 提供的标准 Windows / OpenGL 库（如 `opengl32`、`glu32`、`winmm`、`imm32`、`ws2_32` 等）。

**Linux 上编译示例**

从仓库根目录：

```sh
cmake -S . -B build-mingw \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64-i686.cmake \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-mingw -j$(nproc)
```

如果链接器报 `cannot find -lturbojpeg`，请安装一份提供 `libturbojpeg.a` / `libturbojpeg.dll.a` 的 MinGW-w64 libjpeg-turbo 构建，或调整
`src/CMakeLists.txt` 里 `target_link_libraries` 的库名以匹配你机器上的名字。

---

### 构建项目

项目使用 **CMake** 作为构建系统。`.NET Client Library` 会在构建主项目时被 CMake 自动构建——无需手动 publish！

#### 方式一：Visual Studio 2022+（推荐）

1. **打开项目：**
   - 文件 → 打开 → 文件夹
   - 选择根目录 `MuMain` 文件夹（不是 `src`）

2. **等待 CMake 配置**（自动进行，查看 Output 窗口）

3. **选择构建配置：**
   - 使用下拉框选择 `x86-Debug` 或 `x86-Release`

4. **构建：**
   - 生成 → 生成解决方案
   - 或者按 `Ctrl+Shift+B`

5. **运行/调试：**
   - 把 `Main.exe` 选为启动项
   - 按 `F5` 调试或 `Ctrl+F5` 运行
   - 工作目录会自动设置为 `src/bin`

**注意：** 工作目录已在 `.vs/launch.vs.json` 中配置。如果无效，请确认打开的是根 `MuMain` 文件夹而不是子文件夹。

#### 方式二：CLion

1. **打开项目：**
   - 文件 → 打开
   - 选择根 `MuMain` 文件夹

2. **等待 CMake 配置**（自动进行）

3. **配置工作目录：**
   - Run → Edit Configurations
   - 选择 `Main`
   - 把 "Working directory" 设为构建输出目录（例如 `cmake-build-debug/src/Debug`）
   - 后置构建步骤会把所有游戏资源自动复制到那里

4. **构建与运行：**
   - 点击锤子图标构建
   - 点击播放图标运行

#### 方式三：Rider（命令行生成 CMake + 用 Rider 开发）

Rider 对 C++ 项目没有完整的 CMake 支持，所以需要先生成一份 Visual Studio 解决方案：

1. **生成解决方案**（一次性操作）：
   ```bash
   cmake -B build -G "Visual Studio 17 2022" -A Win32
   ```
   *（根据你安装的 Visual Studio 版本调整 generator）*

2. **在 Rider 中打开：**
   - 文件 → 打开
   - 选择 `build/MuMain.sln`

3. **构建与运行：**
   - 生成 → 生成解决方案
   - 运行 → 运行 'Main'

**重要：** 修改 `CMakeLists.txt` 后，必须重新运行 cmake 命令手动重新生成解决方案。

#### 方式四：命令行构建（Windows）

使用 CMakePresets.json + Ninja（与 IDE 相同，但远快于 MSBuild）：

```powershell
# 配置 x86 构建（仅首次，或 CMakeLists.txt 变化时）
cmake --preset windows-x86

# 构建 Debug
cmake --build --preset windows-x86-debug

# 构建 Release
cmake --build --preset windows-x86-release

# x64 构建用 windows-x64 presets
cmake --preset windows-x64
cmake --build --preset windows-x64-debug
```

**注意：** Ninja Multi-Config 允许在 Debug 与 Release 之间切换而无需重新配置。编译期间资源会自动复制到构建输出目录。

**全新构建（清理）：**
```powershell
Remove-Item -Recurse -Force out
```

**运行可执行文件：**
```powershell
# x86 Debug
./out/build/windows-x86/src/Debug/Main.exe

# x86 Release
./out/build/windows-x86/src/Release/Main.exe
```

#### 方式五：命令行构建（Linux）——尚未可用！

Linux 构建需要先在 CMakePresets.json 中添加 Linux presets。示例工作流：

```bash
# 使用 Ninja 配置 Debug（推荐）
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_EDITOR=OFF

# 构建
cmake --build build

# 切换到 Release：重新配置
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_EDITOR=OFF
cmake --build build
```

**全新构建：**
```bash
rm -rf build
```

**运行可执行文件：**
```bash
./build/src/Main
```

---

### 运行客户端

支持常用启动参数 `/u` 与 `/p`，例如：`main.exe connect /u192.168.0.20 /p55902`。
[OpenMU 启动器](https://github.com/MUnique/OpenMU/releases/download/v0.8.17/MUnique.OpenMU.ClientLauncher_0.8.17.zip)
同样可用。默认连接 localhost 与端口 `44406`。
客户端以自己的版本号 `2.04d` 与序列号 `k1Pk2jcET48mxL3b` 标识。

#### 客户端配置（`config.ini`）
客户端从可执行目录下的 `config.ini` 读取选项：

| 配置节 | 键 | 默认值 | 说明 |
| :--- | :--- | :--- | :--- |
| **`[Render]`** | `CoreProfile` | `1` | **OpenGL 上下文 Profile 开关**<br>`1` = 强制 **OpenGL 3.3 Core Profile**（默认）。所有渲染走 UBO 矩阵、GLSL 3.3 shader、GPU 骨骼蒙皮与 `ImmediateRenderer`（`IR::`）。<br>`0` = 请求 **OpenGL Compatibility Profile** 上下文（重新启用旧的固定管线驱动状态开关，如 `glAlphaTest`/`glEnable(GL_TEXTURE_2D)` 以兼容旧驱动；shader 与 UBO 管线仍然有效）。 |
| **`[UI]`** | `EnableAnimationTaskPool` | `0` | **并行动画处理**<br>`1` = 在拥挤场景（活跃角色 ≥ 20）启用多线程角色动画 tick 池（`AnimationTaskPool`）。<br>`0` = 顺序单线程动画计算。 |
| **`[UI]`** | `Locale` | `"en"` | **界面语言**（`en`、`es`、`pt`、`ru`、`ko`；设置 `zh-CN` 可启用简体中文，见文末「简体中文汉化项目」）。 |
| **`[Login]`** | `Language` | `"eng"` | **本地数据包语言目录前缀**（如 `zh-CN`），驱动 `Data\Local\<前缀>` 下的 bmd/数据包装载，与 `[UI] Locale` 作用域不同。 |
| **`[Camera]`** | `Zoom` | `1735` | **3D 相机默认距离**。 |

#### 命令行参数与选项
- **连接串**：`main.exe connect /u<IP> /p<PORT>`
- **`--enable-taskpool`**：启动时强制启用多线程角色动画更新。
- **`--editor`**：在 `*_mueditor` 构建上启用 ImGui 游戏内编辑器（按 **F12** 切换）。

## 文档

- [GPU Skinning & Core Profile](docs/GPU%20Skinning/README.md) —— 架构、UBO 布局、ImmediateRenderer（`IR::`）、FFP淘汰里程碑目录（`DXP-01` 至 `DXP-27`），以及 Core Profile GL 性能回归系列（`GLP-xx`、`$glstats`）。
- [相机系统](docs/camera-system.md) —— 模式、切换（F9）、配置、视锥剔除、`$details` 面板，以及 3D 相机重构带来的玩法行为变化。
- [DevEditor](docs/dev-editor.md) —— 游戏内调校界面（F12，仅 Debug 构建）。
- [选项窗口与配置](docs/options-window.md) —— 运行时分辨率/窗口切换、滑杆取整，以及选项窗口在 `config.ini` 中保存的内容。
- [构建指南](docs/build/README.md) —— 各平台专属构建笔记。
- [翻译系统](docs/translation-system.md) —— .resx → 生成的 C++ 访问器的流水线如何工作、如何添加字符串或语言、运行时语言切换，以及缓存 UI 字符串的观察者挂钩。

## 简体中文汉化项目 (zh-CN localization)

本仓库内嵌的简体中文汉化如下：

- **MuMain 的三条维护入口**（互不相关、误改即被覆盖）：
  1. **UI 文本** `src/Localization/Game.zh-CN.resx` ← 由官方繁体 `Game.zh-TW.resx` 经 `gen_mumain_zhcn.py`（opencc tw2sp）繁转简生成；缺 key/改译改 zh-TW 源、勿直接改 zh-CN 产物。
  2. **本地数据包名** `src/bin/Data/Local/zh-CN/*_zh-CN.bmd/.txt`（Item/NpcName/Skill…）由 `build_zhcn_pack.py` 生成，由 `[LOGIN] Language=zh-CN` 装载。
  3. **NPC 头顶台词去重** `UI/Chat/Chat.cpp` `CreateChat`（已改 `wcscmp` 判重；这是汉化引入的**唯一**源码改动，上游更新覆盖后需重打）。
- **运行**：`config.ini` 设 `[LOGIN] Language=zh-CN`（bmd 数据装载，长度上限已放宽）与 `[UI] Locale=zh-CN`（resx UI 文本），两者都要设、作用域不同。

> **维护说明**：本分支的汉化由**独立的 `localization` 维护仓库**统一维护（en→zh 主字典 `zh_cn_dict.json`、种子 `zh_cn_dict/*.tsv`、生成脚本 `gen_mumain_zhcn.py` / `build_zhcn_pack.py`、重放流程，与各游戏仓库分开发布）。上面这些 resx / bmd 产物都提交在本仓库内；改汉化请在 `localization` 仓库改字典/种子后重放，勿直接改本仓库产物。

## 贡献

### 编码规则

所有代码改动——无论是人还是 AI 助手——都应遵循
[`docs/CODING_RULES.md`](docs/CODING_RULES.md)。开 PR 之前请先阅读。

AI 编程助手（Claude Code、Cursor、Codex 等）还应阅读
[`AGENTS.md`](AGENTS.md)，它指向相同的规则与构建指南。

## 致谢

  * Webzen
  * Louis
  * Qubit (tuservermu.com.ve)
  * RaGEZONE 与 tuservermu.com.ve 的社区成员贡献的修复
  * [Nitoy](https://github.com/nitoygo) 的 MU Helper