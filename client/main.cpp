#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <ostream>
#include <string>

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
        std::cout << "你发送了：" << line << std::endl;
    }

    // 循环结束回收资源
    close(socket_fd);
    return 0;
}