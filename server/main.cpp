#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <ostream>

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

    // server连接逻辑
    std::cout << "等待客户端连接……" << std::endl;
    int client_fd = accept(server_fd, nullptr, nullptr);
    if (client_fd == -1) {
        std::perror("客户端连接失败");
        close(server_fd);
        return 1;
    }
    std::cout << "客户端已连接，文件描述符为：" << client_fd << std::endl;

    // 消息接收循环
    char buffer[1024];
    while (true) {
        ssize_t received = recv(client_fd, buffer, sizeof(buffer), 0);
        if (received == -1) {
            std::perror("接收出错");
            break;
        } else if (received == 0) {
            std::cout << "客户端发送结束，连接关闭" << std::endl;
            break;
        }
        std::cout << "收到" << received << "Byte(s): ";
        std::cout.write(buffer, received);
        std::cout << "\n";
    }

    close(client_fd);
    close(server_fd);

    return 0;
}