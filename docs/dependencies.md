# 依赖基线

> 更新日期：2026-11-17

## 1. 锁定版本

| 依赖 | 版本 | 用途 | 状态 |
| --- | --- | --- | --- |
| C++ | C++20 | 客户端与原生媒体模块 | 已写入 CMake |
| CMake | 最低 3.25；推荐 4.5.0 | 配置与构建 | 核心工程已接入 |
| MSVC | Visual Studio 2022 x64 | Windows 基线编译器 | CI 使用 |
| Windows | 10 1903 (Build 18362)+ / 11 | `GraphicsCaptureItem` Win32 互操作 | M1 捕获基线 |
| Qt | 6.10.3 | 桌面 UI | CMake 与桌面 CI 已接入 |
| LiveKit C++ SDK | v1.13.1 | WebRTC 发布、订阅与帧接口 | Windows x64 发布端已接入 |
| Node.js | 24.x | 会话 API | M0 后半接入 |
| Fastify | 5.12.8 | 会话 API HTTP 服务 | 已接入 |
| TypeScript | 7.0.2 | 会话 API类型检查与构建 | 已接入 |
| tsx | 4.23.15 | 本地 API 热重载 | 已接入 |
| LiveKit Server | v1.13.11 | 本地 SFU | Compose 已锁定 |
| coturn | 4.19.1-r0 | 本地 TURN | Compose 已锁定 |
| Redis | 8.10.3-alpine | 临时状态与 LiveKit | Compose 已锁定 |
| actions/checkout | v7.0.1 | GitHub Actions checkout | CI 已锁定 |
| actions/setup-node | v7.1.0 | GitHub Actions Node 环境 | CI 已锁定 |
| jurplel/install-qt-action | v4.5.0 (`b344308`) | GitHub Actions Qt 环境 | CI 使用不可变 SHA |

## 2. 版本策略

- 所有生产依赖使用明确版本，不跟踪 `latest`、`main` 或浮动大版本。
- 依赖升级单独提交，并附带构建、测试和性能结果。
- Qt 小版本升级需要重新验证 Windows 10/11、DPI、渲染和打包。
- LiveKit SDK 升级需要重新验证 QG-1、重连、simulcast 和硬件编解码。
- Windows SDK 预编译包固定 SHA-256 `6696ee87c78b958bdbf70e5eb39bf85e1c7528ca2a707e6c13b042c8e4360b9d`；哈希不匹配时配置必须失败。
- CI Action 使用完整版本标签；安全修复通过自动化依赖更新后人工合并。

## 3. 当前本机状态

当前开发机已安装 Node.js 24，但尚未安装 CMake、MSVC、Qt 和 Docker。因此会话 API 在本机与 CI 双重验证，C++ 核心由 Windows GitHub Actions 验证；本机工具链安装完成前，不宣称桌面目标已在本机验证。

## 4. 官方依据

- [Qt 6.10 Windows 支持配置](https://doc.qt.io/qt-6.10/windows.html)
- [Windows Graphics Capture Win32 互操作](https://learn.microsoft.com/windows/win32/api/windows.graphics.capture.interop/)
- [LiveKit C++ SDK v1.13.1](https://github.com/livekit/client-sdk-cpp/releases/tag/v1.13.1)
- [CMake v4.5.0](https://github.com/Kitware/CMake/releases/tag/v4.5.0)
- [actions/checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1)
- [LiveKit Server v1.13.11](https://github.com/livekit/livekit/releases/tag/v1.13.11)
- [coturn 4.19.1-r0](https://github.com/coturn/coturn/releases/tag/docker/4.19.1-r0)
- [Redis 8.10.3](https://github.com/redis/redis/releases/tag/8.10.3)
- [install-qt-action v4.5.0](https://github.com/jurplel/install-qt-action/releases/tag/v4.5.0)
