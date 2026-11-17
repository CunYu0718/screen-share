# Screen Share

面向 Windows 的高性能实时屏幕共享软件。发起者和观看者使用同一款桌面客户端；每个房间最多包含 1 名发起者和 20 名观看者，目标支持 1920×1080@60fps 与 2560×1440@60fps。

> 项目状态：M0 工程基线已通过 CI，M1 已完成捕获、LiveKit 发布/订阅、软件内渲染、六段媒体诊断，以及 30 分钟实机验收报告工具。Intel/NVIDIA/AMD 的真实硬件矩阵尚未执行；当前仓库尚未发布可运行版本，请勿用于生产环境。

## 核心目标

- 软件内发起和观看，不依赖浏览器播放器。
- 采用 SFU 分发，避免发起端为每位观看者重复上传。
- 以接收端实际渲染帧率验收 1080p60 与 2K60。
- 支持分层画质、弱网降级、TURN 兜底和自动重连。
- 默认不录制、不截图、不保存屏幕内容。

## 技术路线

- 客户端：C++20、Qt 6、CMake。
- 屏幕捕获：Windows.Graphics.Capture、Direct3D 11。
- 实时媒体：LiveKit C++ SDK、WebRTC、H.264 硬件编解码。
- 媒体分发：自托管 LiveKit SFU。
- 会话服务：Node.js、TypeScript、Fastify、Redis。
- 网络兜底：coturn。
- 可观测性：OpenTelemetry、Prometheus、Grafana。

## 开发顺序

项目严格按照以下顺序推进：

1. 原生捕获与 2K60 单发单收性能验证。
2. 统一客户端和完整房间闭环。
3. 单发 20 收容量验证与媒体优化。
4. 安全、安装、升级与可运维性。
5. 小范围真实网络试用。

详细依赖、退出条件和任务编号见[开发顺序与里程碑](docs/development-order.md)。任何阶段未通过质量闸门，不进入后续功能开发。

## 文档

- [产品目标与完整需求](docs/product-and-technical-plan.md)
- [系统架构](docs/architecture.md)
- [开发顺序与里程碑](docs/development-order.md)
- [性能与质量闸门](docs/quality-gates.md)
- [M1 实机媒体验收手册](docs/hardware-validation.md)
- [依赖基线](docs/dependencies.md)
- [可观测性与错误契约](docs/observability.md)
- [本地基础设施](infra/README.md)
- [ADR-0001：原生 Windows 客户端与 SFU](docs/adr/0001-native-windows-client-and-sfu.md)
- [贡献指南](CONTRIBUTING.md)
- [安全策略](SECURITY.md)

## 当前范围

MVP 支持 Windows 10 1903（Build 18362）及更高版本和 Windows 11。远程控制、录制、文件传输、剪贴板、摄像头、聊天和跨平台客户端不在首版范围内。

## 构建工程基线

仅构建不依赖 Qt 的核心模块和测试：

```powershell
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

构建 Qt 桌面客户端（已支持选择捕获源、GPU 预览、LiveKit 屏幕轨道发布和软件内观看）：

```powershell
cmake --preset desktop
cmake --build --preset desktop
```

桌面目标要求 Qt 6.10.3。配置时会下载锁定的 LiveKit C++ SDK v1.13.1 Windows x64 预编译包并校验 SHA-256；离线构建可通过 `SCREEN_SHARE_LIVEKIT_SDK_ROOT` 指向已解压的 SDK。完整工具链和版本策略见[依赖基线](docs/dependencies.md)。

当前 M1 界面需要手工输入 LiveKit WebSocket URL 和当前角色的短时令牌。观看端还需填写发起者的 participant identity，客户端将订阅其固定名称 `screen-share` 轨道。令牌签发与加入码将在 M2 接入会话 API；不要把令牌提交到仓库或日志。

状态栏使用固定顺序 `C/E/S/Rx/D/R` 展示 capture、encode、send、receive、decode、render 六段实际 fps，并显示阶段平均耗时、WebRTC 码率、丢包和队列丢帧。指标是技术验证数据，不代表 QG-1 已通过。

发布或观看稳定后，可在两端分别点击“开始 30 分钟验收记录”。客户端会把不含屏幕内容和凭证的端点 JSON 保存到“文档/ScreenShare/validation”；使用 `tools/qg1-evaluate.mjs` 合并两端报告和人工延迟/稳定性证据。完整步骤见[M1 实机媒体验收手册](docs/hardware-validation.md)。

会话 API 开发命令：

```powershell
npm install
npm run typecheck
npm run build
npm test
npm run dev --workspace @screen-share/session-api
```

服务启动后提供 `GET /healthz` 和 `GET /v1/capabilities`。房间创建、加入和令牌签发将在 M2 实现。

## 许可证

本项目采用 [MIT License](LICENSE)。
