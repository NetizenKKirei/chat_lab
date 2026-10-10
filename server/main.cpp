#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <iostream>
#include <system_error>
#include <thread>

#include "common/unique_fd.h"
#include "server/session.h"

int main() {
    // 创建服务器监听socket
    UniqueFd server_fd(socket(AF_INET, SOCK_STREAM, 0));
    if (server_fd.get() == -1) {
        std::perror("socket 创建失败");
        return 1;
    }
    std::cout << "创建成功！文件描述符为：" << server_fd.get() << std::endl;

    // 设置监听socket地址并绑定
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(9000);
    address.sin_addr.s_addr = htonl(INADDR_ANY);

    int bind_result =
        bind(server_fd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    if (bind_result == -1) {
        std::perror("bind 失败");
        return 1;
    }
    std::cout << "绑定成功 0.0.0.0:9000" << std::endl;

    // server监听逻辑
    int listen_result = listen(server_fd.get(), 7);
    if (listen_result == -1) {
        std::perror("监听失败");
        return 1;
    }
    std::cout << "开始监听 0.0.0.0:9000" << std::endl;

    // 循环accept客户端连接
    while (true) {
        std::cout << "等待客户端连接……" << std::endl;
        int client_fd = accept(server_fd.get(), nullptr, nullptr);
        if (client_fd == -1) {
            if (errno == EINTR) {
                continue;
            }
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

    return 0;
}
