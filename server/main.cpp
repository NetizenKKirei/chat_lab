#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <ostream>
#include <thread>
#include <cerrno>
#include <system_error>

#include "common/net.h"
#include "common/protocol.h"

// 单客户端处理
void handle_client(int client_fd) {
    // 消息接收循环
    while (true) {
        Message request;
        if (!recv_message(client_fd, request)) {
            break;
        }
        std::cout << "消息类型：" << static_cast<unsigned int>(request.type)
                  << "\n消息内容: " << request.payload.dump() << "\n";

        Message response;
        response.type = MessageType::Response;
        response.payload = {{"request", "broadcast"}, {"message", "消息接收成功"}};

        if (!send_message(client_fd, response)) {
            break;
        }
    }
    close(client_fd);
}

int main() {
    // 创建服务器监听socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::perror("socket 创建失败");
        return 1;
    }
    std::cout << "创建成功！文件描述符为：" << server_fd << std::endl;

    // 设置监听socket地址并绑定
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    int bind_result = bind(server_fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (bind_result == -1) {
        std::perror("bind 失败");
        close(server_fd);
        return 1;
    }
    std::cout << "绑定成功 127.0.0.1:9000" << std::endl;

    // server监听逻辑
    int listen_result = listen(server_fd, 7);
    if (listen_result == -1) {
        std::perror("监听失败");
        close(server_fd);
        return 1;
    }
    std::cout << "开始监听 127.0.0.1:9000" << std::endl;

    // 循环accept客户端连接
    while (true) {
        std::cout << "等待客户端连接……" << std::endl;
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd == -1) {
            std::perror("接受连接失败");
            break;
        }
        std::cout << "客户端已连接，文件描述符为：" << client_fd << std::endl;

        // 给连接的client创建线程
        try {
            std::thread worker(handle_client, client_fd);
            worker.detach();
        } catch (const std::system_error& error) {
            std::cerr << "创建/分离线程失败：" << error.what() << '\n';
            close(client_fd);
        }
    }

    close(server_fd);
    return 0;
}