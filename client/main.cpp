#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <thread>

#include "common/protocol.h"

// 子线程函数：接收服务端消息
void receive_messages(int socket_fd) {
    while (true) {
        Message message;
        MessageStatus status = recv_message(socket_fd, message);
        if (status == MessageStatus::Rejected) {
            std::cerr << "收到消息格式不合法，已忽略\n";
            continue;
        }
        if (status == MessageStatus::Close) {
            std::cout << "连接中断!\n";
            break;
        }
        std::cout << "\n 收到消息：" << message.payload.dump() << std::endl;
    }
}

// 主机连接函数：解析主机地址并创建socket连接
int connect_to_server(const std::string& host, const std::string& port) {
    // 设置地址筛选条件
    addrinfo hints{};
    hints.ai_family = AF_INET;        // IPv4地址
    hints.ai_socktype = SOCK_STREAM;  // 用于TCP字节流连接
    hints.ai_flags = AI_NUMERICSERV;  // 数字形式端口

    addrinfo* addresses = nullptr;

    // 解析主机地址
    int result = getaddrinfo(host.c_str(), port.c_str(), &hints, &addresses);
    if (result != 0) {
        std::cerr << "地址解析失败" << gai_strerror(result) << "\n";
        return -1;
    }

    // 遍历解析出的地址链表，尝试创建socket并连接
    int socket_fd = -1;
    for (addrinfo* current = addresses; current != nullptr; current = current->ai_next) {
        int candidate = socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (candidate == -1) {
            std::perror("创建socket失败");
            continue;
        }
        if (connect(candidate, current->ai_addr, current->ai_addrlen) == 0) {
            socket_fd = candidate;
            break;
        }
        std::perror("连接失败");
        close(candidate);
    }

    // 释放链表内存
    freeaddrinfo(addresses);
    return socket_fd;
}

int main(int argc, char* argv[]) {
    // 设置启动参数
    std::string host = "127.0.0.1";
    std::string port = "9000";

    if (argc >= 2) {
        host = argv[1];
    }
    if (argc >= 3) {
        port = argv[2];
    }
    if (argc > 3) {
        std::cerr << "参数过多：" << argv[0] << " [主机名或IP] [端口]\n";
        return 1;
    }
    std::cout << "目标服务器：" << host << ":" << port << '\n';

    // 创建客户端socket
    int socket_fd = connect_to_server(host, port);
    if (socket_fd == -1) {
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
        } else if (line.find("/to ") == 0) {
            std::size_t separator = line.find(" ", 4);

            if (separator == std::string::npos) {
                std::cout << "请输入合法格式：/to [用户名] [正文]\n";
                continue;
            }
            std::string receiver = line.substr(4, separator - 4);
            std::string text = line.substr(separator + 1);
            if (receiver.empty() || text.empty()) {
                std::cout << "用户名和正文不能为空";
                continue;
            }
            request.type = MessageType::Private;
            request.payload = {{"to", receiver}, {"text", text}};
        } else {
            std::cout << "不支持该操作\n";
            continue;
        }

        MessageStatus status = send_message(socket_fd, request);
        if (status == MessageStatus::Rejected) {
            std::cerr << "消息发送被拒，请重试\n";
            continue;
        }
        if (status == MessageStatus::Close) {
            std::cout << "连接中断!\n";
            break;
        }
    }

    // 循环结束回收资源
    shutdown(socket_fd, SHUT_RDWR);
    receiver.join();
    close(socket_fd);
    return 0;
}