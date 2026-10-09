# chat_lab

基于 C++17 和 TCP 的命令行聊天程序，支持用户名登录、在线列表、群聊与私聊。服务端运行在 Debian 云主机上，客户端可直接运行，也可在独立 Docker 容器中运行。

- [通信协议](docs/protocol.md)：帧格式、消息字段、长度限制和错误码。
- [设计说明](docs/design.md)：模块分工、线程、锁和资源管理。

## 1. 环境与编译

项目使用 Debian 13、Clang、CMake 和 Ninja。以下命令在云主机执行：

```bash
sudo apt update
sudo apt install -y build-essential clang cmake ninja-build libssl-dev nlohmann-json3-dev
```

在项目根目录编译：

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

生成 `build/chat_server` 和 `build/chat_client`。修改源码后重新编译，并重启相应程序。

## 2. 启动服务端与本机客户端

一个云主机终端启动服务端：

```bash
./build/chat_server
```

服务端监听 `0.0.0.0:9000`。另一个终端启动客户端：

```bash
./build/chat_client
```

客户端默认连接 `127.0.0.1:9000`，也可指定主机名或 IPv4 地址及端口：

```bash
./build/chat_client localhost 9000
```

服务端地址 `0.0.0.0` 表示监听范围，不应作为客户端目标。服务端在云主机时，应在同一云主机终端执行以上本机连接命令。

## 3. 用 Docker 启动两个客户端

先检查 Docker、镜像和网络：

```bash
docker ps
docker image ls chatlab:dev
docker network ls
```

尚未安装 Docker 时执行 `sudo apt install -y docker.io`。若当前用户没有访问权限，可由管理员配置 Docker 使用权限；使用 `sudo usermod -aG docker "$USER"` 加组后需要重新登录。

镜像或网络不存在时，分别执行：

```bash
docker build -t chatlab:dev docker/
docker network create chatnet
```

服务端保持运行，在两个云主机终端分别执行：

```bash
bash scripts/client.sh
```

每次启动一个客户端容器。脚本使用 `chatnet` 网络，将项目目录挂载到容器 `/work`，运行云主机已经编译好的程序。默认连接 `host.docker.internal:9000`，该名称由脚本映射到宿主机入口地址。也可传入目标：

```bash
bash scripts/client.sh host.docker.internal 9000
```

直接执行脚本时，先设置权限：

```bash
chmod +x scripts/client.sh
./scripts/client.sh
```

修改 C++ 源码只需重新编译、重启客户端；改变镜像内依赖时再重新构建镜像。挂载目录是同一份项目文件，不是副本。

## 4. 客户端命令

| 命令 | 示例 | 作用 |
| --- | --- | --- |
| `/login 用户名` | `/login alice` | 为当前连接登录；合法名称为 1～32 个 ASCII 字母、数字或下划线，区分大小写 |
| `/list` | `/list` | 查询排序后的在线名单，包含自己 |
| `/all 正文` | `/all 大家好` | 群发给其他已登录用户，发送者只收到操作结果 |
| `/to 用户名 正文` | `/to bob hello world` | 私聊指定用户，正文可包含空格 |
| `/quit` | `/quit` | 退出客户端并关闭连接 |

查询和聊天需要先登录。一个连接不能重复登录，在线名称不能重名。正文非空且最多 1 MiB；整个 JSON 负载最多 4 MiB。客户端通过独立线程接收消息，目前直接显示 JSON。

正文超过 1 MiB 且整个 JSON 未超过 4 MiB 时，服务端返回 `MESSAGE_TOO_LARGE`，可以继续发送其他请求。整个 JSON 超过 4 MiB 时，客户端在本地拒绝发送并允许重新输入。

无人接收群聊、私聊目标离线或发送失败时返回错误。服务器断开后接收线程会提示，主线程可能仍在等待键盘输入，此时输入 `/quit` 或在空白输入处按 `Ctrl+D` 结束。


