# 可观测性与错误契约

## 1. 目标

可观测性用于回答三个问题：用户是否成功连上、实际看到了什么质量、资源是否接近容量上限。指标和日志只能描述技术状态，不能采集屏幕内容或用户输入。

## 2. 客户端指标

| 指标 | 类型 | 标签 | 说明 |
| --- | --- | --- | --- |
| `screenshare_client_frames_total` | Counter | `stage` | capture/encode/send/receive/decode/render 累计帧数 |
| `screenshare_client_frame_rate` | Gauge | `stage` | 每个媒体阶段的实际 fps |
| `screenshare_client_stage_duration_seconds` | Histogram | `stage` | 单帧在各阶段的处理耗时 |
| `screenshare_client_end_to_end_latency_seconds` | Histogram | `quality` | 可验证的端到端画面延迟 |
| `screenshare_client_video_bitrate_bps` | Gauge | `direction`, `quality` | 实际媒体码率 |
| `screenshare_client_packet_loss_ratio` | Gauge | `direction` | 发送或接收丢包比例 |
| `screenshare_client_reconnects_total` | Counter | `reason`, `result` | ICE restart 或重新加入次数 |
| `screenshare_client_freezes_total` | Counter | `duration_bucket` | 观看端画面冻结次数 |

`stage` 只能使用固定值，禁止把会话 ID、设备 ID 或错误消息放入指标标签。

## 3. 会话服务指标

| 指标 | 类型 | 标签 | 说明 |
| --- | --- | --- | --- |
| `screenshare_rooms_active` | Gauge | 无 | 当前活跃房间数 |
| `screenshare_room_viewers` | Histogram | 无 | 每个房间的观看人数分布 |
| `screenshare_room_admission_total` | Counter | `result` | accepted/room_full/rejected |
| `screenshare_token_issuance_total` | Counter | `role`, `result` | 主持人或观看令牌签发结果 |
| `screenshare_join_attempts_total` | Counter | `result` | 加入尝试结果 |
| `screenshare_host_recovery_total` | Counter | `result` | 主持人恢复窗口结果 |

HTTP 请求耗时、状态码和并发连接优先使用 OpenTelemetry HTTP 语义约定，不重复创造同义指标。

## 4. 结构化日志字段

允许字段：

- `timestamp`、`level`、`service`、`version`、`event`。
- `request_id`、`trace_id`、`span_id`。
- 经过单向散列和定期轮换盐处理的 `session_hash`、`participant_hash`。
- `role`、`result`、`error_code`、`duration_ms`。
- 非识别性的系统类别，例如 GPU 厂商和编码器类型。

禁止字段：

- 访问令牌、刷新令牌、加入码和完整 SDP。
- 屏幕帧、截图、窗口标题、进程参数和剪贴板。
- IP 地址、邮箱、用户名、设备序列号和未经散列的会话/参与者 ID。
- 任意用户输入或可能包含个人信息的自由文本。

## 5. 公共错误码

| 错误码 | HTTP | 含义 |
| --- | --- | --- |
| `invalid_request` | 400 | 请求结构或字段无效 |
| `unauthorized` | 401/403 | 凭证缺失、无效或角色无权限 |
| `room_not_found` | 404 | 房间不存在或已结束 |
| `route_not_found` | 404 | API 路由不存在 |
| `room_full` | 409 | 已达到 20 名观看者上限 |
| `token_expired` | 401 | 短时令牌已过期 |
| `internal_error` | 500 | 不向客户端泄露内部细节的服务错误 |

公共错误响应固定为：

```json
{
  "error": {
    "code": "room_full",
    "message": "Room has reached its viewer limit",
    "requestId": "opaque-request-id"
  }
}
```

`message` 用于稳定的用户提示，不得直接返回异常堆栈、数据库错误或第三方响应正文。

## 6. 告警原则

- 以用户影响告警：连接失败率、首帧超时、冻结、房间中断优先于单个进程 CPU。
- 容量告警必须早于硬上限：SFU CPU/内存或出口带宽剩余 30% 时进入预警。
- 单个弱网用户不触发全局事故告警，但应计入质量分布。
- 告警中只引用散列标识，排障人员按受控流程关联原始会话。
