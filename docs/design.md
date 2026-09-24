# 设计说明

## 阶段0：环境配置
> 记录在环境配置中遇到的文档未提及/按操作步骤无法解决的问题
- 每次启动clab云主机默认未连接互联网，需要通过 `clabcli connect` 命令登录网关
- 除 `common/net.cpp` 和 `common/protocol.cpp`外，还需要创建`server/main.cpp` `client/main.cpp` `tests/protocol_test.cpp`并写入最小程序，否则CMake也会报错
- 直连docker hub超时，需要配置镜像加速
- 创建云主机时OS镜像需要与Dockerfile配置一致

##
