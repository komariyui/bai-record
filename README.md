下面是一份可直接放到 GitHub `README.md` 的完整介绍：

# 由白记录 —— 记录你的创作过程

#### BAI Record —— Document your creative process

由白记录是一款面向数字艺术创作者的本地绘画过程记录工具，支持实时记录 SAI、SAI2 与 Photoshop 画布变化，并将创作过程保存为连续帧，方便后续导出为绘画过程视频。

它不依赖传统屏幕录制，而是直接读取绘画软件中的画布数据，尽量减少对画笔操作、数位板输入和软件性能的影响。

官网：[https://bai.wuhupoo.cn](https://bai.wuhupoo.cn)

<img width="2678" height="1734" alt="Group 5" src="https://github.com/user-attachments/assets/e9ba214e-9871-4a5f-bb02-361b6a7e2724" />

---

## 主要功能

- 记录 SAI 画布
- 记录 SAI2 多画布
- 记录 Photoshop 画布
- 支持 Photoshop 多文档选择
- 一笔一帧记录
- 支持 Ctrl+Z 撤销操作记录
- 实时画布预览
- 历史项目管理
- 继续录制已有项目
- 项目文件夹和源文件快速打开
- 画布封面自动生成
- 帧素材回收站
- 视频导出
- 导出范围选择
- 帧时长、帧率、硬件编码等导出选项
- APM 操作记录
- 中英文、日文、法文、印地语等多语言支持
- 自动根据系统语言初始化界面语言
- Photoshop/SAI 进程性能优先级优化

---

## 支持的绘画软件

### SAI / SAI2

当前源码中已内置以下版本的结构识别与画布读取支持：

#### SAI 1.x

- SAI Ver.1.2.5
- SAI Ver.1.2.6-Beta.3
- SAI Ver.1 Legacy（32-bit）

#### SAI2

- SAI Ver.2 Preview.2016.11.12
- SAI Ver.2 Preview.2017.08.20
- SAI Ver.2 Preview.2018.06.14b
- SAI Ver.2 Preview.2018.09.20
- SAI Ver.2 Preview.2020.01.25
- SAI Ver.2 Preview.2022.02.07
- SAI Ver.2 Preview.2023.07.11
- SAI Ver.2 Preview.2024.02.22
- SAI Ver.2 Preview.2024.08.14
- SAI Ver.2 Preview.2024.11.23
- SAI Ver.2 Preview.2026.07.05
- SAI Ver.2 Preview.2026.08.13
- SAI Ver.2 Saier build

---

### Photoshop

Photoshop 根据版本使用不同的桥接方案。

| Photoshop 版本 | 采集方式 | 状态 |
|---|---|---|
| Photoshop CS6–2022 | CEP + ExtendScript | 支持旧版环境 |
| Photoshop 2023 及以上 | UXP + Imaging API | 推荐方案 |
| Photoshop 2024 | UXP / 可选 C++ Plugin SDK | 支持增强采集 |
| Photoshop 2025–2026 | UXP 能力检测 | 根据实际 API 能力运行 |

#### Photoshop CS6–2022

旧版 Photoshop 使用 CEP 扩展和 ExtendScript 桥接：

- CEP 宿主版本：`PHXS [13.0, 23.99]`
- Photoshop CS6 对应版本号：13.x
- Photoshop 2022 对应版本号：23.x
- 使用隐藏后台扩展维持桥接连接
- 通过 ExtendScript 生成画布 PNG
- 不需要 COM
- 不需要用户复制粘贴画布
- 不使用 Photoshop 的保存副本流程

#### Photoshop 2023 及以上

新版 Photoshop 使用 UXP 插件和 Imaging API：

- 使用 `imaging.getPixels()` 读取画布像素
- 通过本机回环地址发送数据：
  - `127.0.0.1`
  - `localhost`
- 支持当前活动文档和多文档选择
- 通过文档 ID 保持用户选择的画布
- 不依赖屏幕截图
- 不依赖 COM
- 不依赖 JSX 导出
- 不需要保存 Photoshop 文档副本

#### Photoshop 2024 C++ Plugin SDK

项目同时提供 Photoshop C++ Plugin SDK 采集路径，主要用于 Photoshop 2024 环境：

- `YoubaiCanvasCapture.8bf`
- `YoubaiCanvasListener.8li`
- 使用 Photoshop SDK 的 `ReadScaledPixels`
- 读取合成后的 RGBA 画布数据
- 通过本地桥接服务传输到主程序
- 不调用 COM
- 不调用 JSX
- 不执行文档保存
- 不弹出保存副本窗口

该方案需要将插件安装到 Photoshop 的 `Plug-ins` 目录，并重启 Photoshop。

---

## 关键技术

### SAI 进程内存读取

SAI 模式不使用屏幕录制，也不通过窗口截图获取画布。

程序通过 Windows 进程读取接口访问 SAI 的画布结构：

```text
SAI 进程
   ↓
ProcessReader
   ↓
SAIEngine
   ↓
CanvasInfo
   ↓
像素缓冲区 / Tile Map
   ↓
QImage
```

主要技术包括：

- `OpenProcess`
- `ReadProcessMemory`
- SAI 版本哈希识别
- 画布链表读取
- 多画布结构识别
- Tile Map 拼接
- BGRA/RGBA 像素转换
- 画布尺寸和缩放级别检测

对于 SAI2，程序会读取多个 tile，并重新拼接成完整画布，避免使用缩略图或屏幕预览。

---

### Photoshop UXP Imaging API

Photoshop 2023 及以上版本使用 UXP 插件读取画布：

```text
Photoshop Canvas
   ↓
UXP Imaging API
   ↓
RGBA Pixel Data
   ↓
本机 HTTP Bridge
   ↓
Qt 主程序
   ↓
PNG 帧文件
```

插件和主程序之间只通过本机回环通信：

```text
http://127.0.0.1:38127
http://localhost:38127
```

网络数据不会发送到远程服务器，核心画布采集过程在本机完成。

---

### CEP + ExtendScript 兼容方案

为了兼容 Photoshop CS6 至 Photoshop 2022，项目保留 CEP 方案：

- CEP 面板作为隐藏后台扩展运行
- Photoshop 激活时自动启动
- 主程序通过本机桥接服务发出采集请求
- CEP 调用 ExtendScript
- ExtendScript 在后台生成 PNG
- 主程序异步读取完成的 PNG

该方案不依赖 Photoshop 版本特定的 COM ProgID，也不会弹出保存对话框。

---

### 异步 PNG 保存

帧文件由 `VideoSequenceWriter` 管理：

```text
项目名_000001.png
项目名_000002.png
项目名_000003.png
```

支持：

- 普通 PNG 帧存储
- 差分帧存储
- 后台写入
- 待保存队列
- 录制停止时等待剩余帧保存完成
- 失败帧错误提示
- 项目帧数量恢复

---

### 项目和素材管理

每个绘画项目都有独立目录：

```text
工程目录/
├─ project.json
├─ apm.json
├─ project_cover.png
├─ project_000001.png
├─ project_000002.png
└─ .recycle/
```

项目元数据包括：

- 项目名称
- 画布名称
- 软件类型
- 软件版本
- 源文件路径
- 帧文件名前缀
- 最后更新时间
- 项目封面

主页支持：

- 历史项目列表
- 继续录制
- 导出视频
- 打开项目文件夹
- 打开源文件
- 右键删除项目
- 素材回收站
- 删除素材恢复
- 清空素材回收站

---



## 支持的运行环境

### 操作系统

- Windows 10
- Windows 11
- 64 位系统推荐
- SAI1 兼容 32 位目标进程
- SAI2 使用 64 位目标进程

### 开发环境

- C++17
- CMake 3.16+
- Qt 6
- Qt Widgets
- Qt Quick
- Qt Quick Controls 2
- Qt QML
- Qt SVG
- Qt Network
- MinGW 64-bit
- Visual Studio 2022
- Photoshop C++ Plugin SDK

项目当前主要使用 Qt 6.11.1 MinGW 64-bit 进行构建验证。

---

## 安装与使用流程

### SAI / SAI2

1. 启动 SAI 或 SAI2。
2. 启动由白记录。
3. 程序自动检测绘画软件和画布。
4. 在主页选择快速开始记录。
5. 选择需要记录的画布。
6. 点击开始录制。
7. 每完成一笔，程序自动保存一帧。
8. 停止录制后，可在主页导出视频。

### Photoshop 2023 及以上

1. 启动 Photoshop。
2. 启动由白记录。
3. 安装或启用 UXP 桥接插件。
4. 在 Photoshop 的“增效工具”中打开对应插件。
5. 回到由白记录，等待插件连接。
6. 选择 Photoshop 画布。
7. 点击开始录制。

### Photoshop CS6–2022

1. 安装 CEP 旧版桥接扩展。
2. 重启 Photoshop。
3. 确认 CEP 后台扩展已经启动。
4. 启动由白记录。
5. 选择 Photoshop 文档。
6. 开始录制画布。

---


## 日志与故障排查

程序日志默认保存到：

```text
%APPDATA%\Youbai\SAI Record Youbai\crash_debug.log
```

常见日志关键词：

```text
[stroke]
[capture]
[performance]
[photoshop-uxp]
[homepage]
```

其中：

- `[stroke]`：笔画开始、结束和去重信息
- `[capture]`：画布读取和帧写入信息
- `[performance]`：绘画软件优先级调整信息
- `[photoshop-uxp]`：Photoshop 插件桥接信息
- `[homepage]`：主页和历史项目加载信息

---

## 隐私说明

由白记录的核心画布采集在本地完成：

- 不使用云端画布存储
- 不上传绘画内容
- 不通过屏幕录制采集桌面
- Photoshop 采集通过本机插件和本地桥接完成
- SAI 采集通过本机进程读取完成
- 视频和 PNG 帧保存在用户指定的本地工程目录

程序可能根据功能需要访问更新、赞助或服务相关页面，但画布帧采集本身不会上传到远程服务器。

![Start](https://raw.githubusercontent.com/komariyui/bai-record/main/start.png)


