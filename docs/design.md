# 设计说明

## 阶段0：环境配置
> 记录在环境配置中遇到的文档未提及/按操作步骤无法解决的问题
- 每次启动clab云主机默认未连接互联网，需要通过 `clabcli connect` 命令登录网关
- 除 `common/net.cpp` 和 `common/protocol.cpp`外，还需要创建`server/main.cpp` `client/main.cpp` `tests/protocol_test.cpp`并写入最小程序，否则CMake也会报错
- 直连docker hub超时，需要配置镜像加速
- 创建云主机时OS镜像需要与Dockerfile配置一致

## 阶段1：命令行聊天


### 帧结构

使用手册中方案A：固定8字节头部 + UTF-8 JSON负载。用长度字段确定每条消息的结束位置。

| Index | 字段 | 长度 | 内容 |
| --- | --- | --- | --- |
| 0～1 | 魔数 | 2 字节 | ASCII `CH` |
| 2 | 版本 | 1 字节 | 固定为 `1` |
| 3 | 消息类型 | 1 字节 | 无符号编号，见下表 |
| 4～7 | 负载长度 | 4 字节 | 无符号 32 位整数，大端序 |
| 8～ | 负载 | 长度字段指定的字节数 | UTF-8 JSON |



发送端先序列化JSON、检查长度，再组装头部，使用 `send_all` 完整发送头部和负载。接收端使用 `recv_exact` 先读8字节校验头部和长度，再分配空间读取负载并解析JSON。


### 消息类型与字段

负载均为JSON对象。

| 编号 | 类型 | 客户端请求负载 | 服务端行为 |
| --- | --- | --- | --- |
| 1 | `Login` | `{"username":"alice"}` | 检查用户名和重名情况，为当前连接绑定身份 |
| 2 | `List` | `{}` | 返回当前在线用户名列表 |
| 3 | `Broadcast` | `{"text":"大家好"}` | 发送给其他所有已登录用户 |
| 4 | `Private` | `{"to":"bob","text":"你好"}` | 向目标用户发送讯息 |
| 5 | `Response` | 仅作为服务端类型 | 服务端返回成功响应 |
| 6 | `Error` | 仅作为服务端类型 | 服务端返回错误响应 |

服务端响应和返回格式：

| 类型 | 场景 | JSON 示例 |
| --- | --- | --- |
| `Response` | 登录成功 | `{"request":"login","username":"alice"}` |
| `Response` | 在线列表 | `{"request":"list","users":["alice","bob"]}` |
| `Response` | 群发处理成功 | `{"request":"broadcast"}` |
| `Response` | 私聊处理成功 | `{"request":"private"}` |
| `Broadcast` | 群聊发送 | `{"from":"alice","text":"大家好"}` |
| `Private` | 私聊发送 | `{"from":"alice","text":"你好"}` |
| `Error` | 操作失败 | `{"request":"private","code":"USER_OFFLINE","message":"目标用户不在线"}` |


