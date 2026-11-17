# 依赖基线

> 更新日期：2026-11-17

## 1. 锁定版本

| 依赖 | 版本 | 用途 | 状态 |
| --- | --- | --- | --- |
| C++ | C++20 | 客户端与原生媒体模块 | 已写入 CMake |
| CMake | 最低 3.25；推荐 4.5.0 | 配置与构建 | 核心工程已接入 |
| MSVC | Visual Studio 2022 x64 | Windows 基线编译器 | CI 使用 |
| Qt | 6.12.0 LTS | 桌面 UI | CMake 可选目标已接入 |
| LiveKit C++ SDK | v1.13.1 | WebRTC 发布、订阅与帧接口 | M1 接入 |
| Node.js | 24.x | 会话 API | M0 后半接入 |
| actions/checkout | v7.0.1 | GitHub Actions checkout | CI 已锁定 |

## 2. 版本策略

- 所有生产依赖使用明确版本，不跟踪 `latest`、`main` 或浮动大版本。
- 依赖升级单独提交，并附带构建、测试和性能结果。
- Qt 小版本升级需要重新验证 Windows 10/11、DPI、渲染和打包。
- LiveKit SDK 升级需要重新验证 QG-1、重连、simulcast 和硬件编解码。
- CI Action 使用完整版本标签；安全修复通过自动化依赖更新后人工合并。

## 3. 当前本机状态

当前开发机尚未安装 CMake、MSVC、Qt 和 Docker，因此核心工程首先由 Windows GitHub Actions 验证。本机工具链安装完成前，不宣称桌面目标已在本机验证。

## 4. 官方依据

- [Qt 6.12 Windows 支持配置](https://doc.qt.io/qt-6/windows.html)
- [LiveKit C++ SDK v1.13.1](https://github.com/livekit/client-sdk-cpp/releases/tag/v1.13.1)
- [CMake v4.5.0](https://github.com/Kitware/CMake/releases/tag/v4.5.0)
- [actions/checkout v7.0.1](https://github.com/actions/checkout/releases/tag/v7.0.1)
