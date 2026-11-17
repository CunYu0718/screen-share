# 本地基础设施

该目录用于 M0/M1 开发环境，不是生产部署模板。

## 组件

- LiveKit SFU `v1.13.11`：信令与媒体路由。
- Redis `8.10.3-alpine`：LiveKit 节点状态与后续会话状态。
- coturn `4.19.1-r0`：开发环境 UDP/TCP TURN 中继。

## 启动

1. 复制 `infra/.env.example` 为 `infra/.env`。
2. 为 API/TURN 三个密钥变量填写仅用于本机开发的高熵随机值。
3. 单机验证保持 `LIVEKIT_NODE_IP=127.0.0.1`；双机局域网验证必须改为运行 Docker 的宿主机 LAN IPv4，例如 `192.168.1.20`。不要填写容器 IP。
4. 验证配置并启动：

```powershell
docker compose --env-file infra/.env -f infra/compose.yaml config --quiet
docker compose --env-file infra/.env -f infra/compose.yaml up -d
docker compose --env-file infra/.env -f infra/compose.yaml ps
```

双机验证还需在基础设施宿主机防火墙中允许下表端口，并让两台客户端使用 `ws://<LIVEKIT_NODE_IP>:7880`。该开发配置没有 TLS，不能跨不可信网络或暴露到公网。

## 生成 M1 验收令牌

仓库提供仅用于 M1 本地验证的短时令牌工具。它从被 Git 忽略的 `infra/.env` 读取 API 密钥，不会把 API secret 放入命令行或输出：

```powershell
npm run validation:tokens -- --env-file infra/.env --server-url ws://192.168.1.20:7880 --room qg1-validation
```

输出中的字段对应客户端：

- 发起端：使用 `serverUrl`、`host.token`；发起者 identity 为 `host.identity`。
- 观看端：使用 `serverUrl`、`viewer.token`，并把 `viewer.publisherIdentity` 填入“发起者身份”。
- 默认有效期 60 分钟，可用 `--ttl-minutes 35` 至 `120` 调整。

主持人令牌只能发布、不能订阅；观看者令牌只能订阅、不能发布；两者均不能发布 data channel 或修改自身元数据。令牌输出仍属于临时凭证，不得保存到报告、提交到 Git、粘贴到 Issue 或发送给无关人员。生产令牌签发仍属于 M2，不能复用这个命令行工具。

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
- `use_external_ip: false` 配合显式 `LIVEKIT_NODE_IP` 只适合本机或受信局域网验证，公网部署必须重新配置。
- Redis 不映射到主机端口，不能直接暴露公网。

## 生产环境禁止直接复用

生产部署还需要可信 TLS、密钥管理、独立公网 IP、容量隔离、监控、备份、滚动升级和防火墙策略。不得只修改密码后直接上线此 Compose。
