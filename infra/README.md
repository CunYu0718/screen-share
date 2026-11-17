# 本地基础设施

该目录用于 M0/M1 开发环境，不是生产部署模板。

## 组件

- LiveKit SFU `v1.13.11`：信令与媒体路由。
- Redis `8.10.3-alpine`：LiveKit 节点状态与后续会话状态。
- coturn `4.19.1-r0`：开发环境 UDP/TCP TURN 中继。

## 启动

1. 复制 `infra/.env.example` 为 `infra/.env`。
2. 为三个变量填写仅用于本机开发的高熵随机值。
3. 验证配置并启动：

```powershell
docker compose --env-file infra/.env -f infra/compose.yaml config --quiet
docker compose --env-file infra/.env -f infra/compose.yaml up -d
docker compose --env-file infra/.env -f infra/compose.yaml ps
```

停止服务：

```powershell
docker compose --env-file infra/.env -f infra/compose.yaml down
```

## 端口

| 端口 | 协议 | 用途 |
| --- | --- | --- |
| 7880 | TCP | LiveKit WebSocket/HTTP |
| 7881 | TCP | LiveKit WebRTC TCP |
| 7882 | UDP | LiveKit WebRTC UDP |
| 3478 | TCP/UDP | coturn 开发中继 |
| 49160–49200 | UDP | coturn relay 端口范围 |

## 安全边界

- `.env` 已被 Git 忽略；仓库只保留空值示例。
- LiveKit 密钥通过环境变量传入，不写入 YAML。
- coturn 使用 TURN REST shared secret，不启用匿名访问。
- 开发环境明确禁用 TURN TLS/DTLS；M3 将使用真实域名、可信证书并验证 TLS 443。
- `use_external_ip: false` 只适合本机或局域网验证，公网部署必须重新配置。
- Redis 不映射到主机端口，不能直接暴露公网。

## 生产环境禁止直接复用

生产部署还需要可信 TLS、密钥管理、独立公网 IP、容量隔离、监控、备份、滚动升级和防火墙策略。不得只修改密码后直接上线此 Compose。
