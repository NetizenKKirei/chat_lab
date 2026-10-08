#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <thread>

#include "common/protocol.h"

void receive_messages(int socket_fd) {
    while (true) {
        Message message;
        if (!recv_message(socket_fd, message)) {
            std::cout << "\n 服务器连接已结束\n";
            break;
        }
        std::cout << "\n 收到消息：" << message.payload.dump() << std::endl;
    }
}

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

    std::thread receiver(receive_messages, socket_fd);

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

        // 发送请求
        Message request;

        // 识别请求类型
        if (line.find("/login ") == 0) {
            request.type = MessageType::Login;
            request.payload = {{"username", line.substr(7)}};
        } else if (line == "/list") {
            request.type = MessageType::List;
            request.payload = nlohmann::json::object();
        } else if (line.find("/all ") == 0) {
            request.type = MessageType::Broadcast;
            request.payload = {{"text", line.substr(5)}};
        } else {
            std::cout << "不支持该操作\n";
            continue;
        }

        if (!send_message(socket_fd, request)) {
            break;
        }
    }

    // 循环结束回收资源
    shutdown(socket_fd, SHUT_RDWR);
    receiver.join();
    close(socket_fd);
    return 0;
}