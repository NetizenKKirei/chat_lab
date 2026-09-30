#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <string>

#include "common/net.h"

int main() {
    // 创建客户端socket
    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        std::perror("socket 创建失败");
        return 1;
    }
    std::cout << "创建成功！文件描述符为：" << socket_fd << std::endl;

    // 设置服务器地址
    sockaddr_in server_address{};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(9000);
    server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    // 连接服务器
    int connect_result = connect(socket_fd, reinterpret_cast<const sockaddr*>(&server_address),
                                 sizeof(server_address));
    if (connect_result == -1) {
        std::perror("连接失败");
        close(socket_fd);
        return 1;
    }
    std::cout << "已连接服务器" << std::endl;

    char buffer[4];  // 接收数组

    // 本地输入循环
    while (true) {
        std::string line;
        std::cout << "请输入文本：";
        if (!std::getline(std::cin, line)) {
            std::cout << "输入结束或读取失败！\n";
            break;
        }
        if (line.empty()) {
            std::cout << "输入不能为空！\n";
            continue;
        } else if (line == "/quit") {
            std::cout << "对话结束，再见！\n";
            break;
        }

        // 循环发送
        if (!send_all(socket_fd, line.data(), line.size())) {
            break;
        }

        // 接收回显
        unsigned long total_recv = 0;
        while (total_recv < line.size()) {
            ssize_t received = recv(socket_fd, buffer, sizeof(buffer), 0);
            if (received == -1) {
                std::perror("接收错误");
                break;
            } else if (received == 0) {
                std::cout << "服务端发送结束，连接关闭" << std::endl;
                break;
            }
            std::cout << "收到" << received << "Byte(s): ";
            std::cout.write(buffer, received);
            std::cout << "\n";
            total_recv += received;
        }
        if (total_recv < line.size()) {  // 接收异常退出时跳出外层循环
            break;
        }
    }

    // 循环结束回收资源
    close(socket_fd);
    return 0;
}