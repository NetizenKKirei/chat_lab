#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstddef>
#include <cstdio>
#include <iostream>
#include <ostream>
// 线程管理
#include <cerrno>
#include <system_error>
#include <thread>
// 在线用户管理
#include <algorithm>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "common/error_codes.h"
#include "common/net.h"
#include "common/protocol.h"

std::unordered_set<std::string> online_users;  // 在线用户列表
std::mutex users_mutex;                        // 线程加锁

// 错误响应函数
Message make_error(const std::string& request_name, const std::string& code,
                   const std::string& text) {
    Message response;
    response.type = MessageType::Error;
    response.payload = {{"request", request_name}, {"code", code}, {"message", text}};
    return response;
}

// 用户名检查函数
bool valid_username(const std::string& name) {
    if (name.empty() || name.size() > 32) {
        return false;
    }
    for (char ch : name) {
        bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                       (ch >= '0' && ch <= '9') || ch == '_';
        if (!allowed) {
            return false;
        }
    }
    return true;
}

// 登录处理函数
Message handle_login(const Message& request, std::string& username) {
    // 检查当前连接是否已注册用户名
    if (!username.empty()) {
        return make_error("login", ErrorCode::AlreadyLoggedIn, "当前连接已登陆");
    }
    // 检测传入参数是否空白或不为string
    if (!request.payload.contains("username") || !request.payload["username"].is_string()) {
        return make_error("login", ErrorCode::InvalidArgument, "username必须为字符串");
    }
    // 检测username命名是否合法
    std::string requested_name = request.payload["username"].get<std::string>();
    if (!valid_username(requested_name)) {
        return make_error("login", ErrorCode::InvalidArgument, "username不合法");
    }
    // 尝试注册用户名
    bool inserted = false;
    {
        std::lock_guard<std::mutex> lock(users_mutex);
        inserted = online_users.insert(requested_name).second;
    }
    if (!inserted) {
        return make_error("login", ErrorCode::UsernameTaken, "username已占用");
    }
    // 注册成功后保存身份并返回响应
    username = requested_name;
    Message response;
    response.type = MessageType::Response;
    response.payload = {{"request", "login"}, {"username", username}};
    return response;
}

// 在线用户查询函数
Message handle_list(const std::string& username) {
    if (username.empty()) {
        return make_error("list", ErrorCode::NotLoggedIn, "请先登录");
    }
    // 构建查询时刻的在线用户列表快照users
    std::vector<std::string> users;
    {
        std::lock_guard<std::mutex> lock(users_mutex);
        for (const auto& name : online_users) {
            users.push_back(name);
        }
    }
    std::sort(users.begin(), users.end());

    // 返回响应
    Message response;
    response.type = MessageType::Response;
    response.payload = {{"request", "list"}, {"users", users}};
    return response;
}

// 单客户端处理
void handle_client(int client_fd) {
    std::string username;

    // 消息接收循环
    while (true) {
        Message request;
        if (!recv_message(client_fd, request)) {
            break;
        }
        std::cout << "消息类型：" << static_cast<unsigned int>(request.type)
                  << "\n消息内容: " << request.payload.dump() << "\n";

        Message response;
        if (request.type == MessageType::Login) {
            response = handle_login(request, username);
        } else if (request.type == MessageType::List) {
            response = handle_list(username);
        } else {
            response.type = MessageType::Error;
            response.payload = {{"request", "unknown"},
                                {"code", ErrorCode::UnexpectedType},
                                {"message", "该请求暂未实现"}};
        }
        if (!send_message(client_fd, response)) {
            break;
        }
    }
    // 客户端关闭前释放用户名
    if (!username.empty()) {
        std::lock_guard<std::mutex> lock(users_mutex);
        online_users.erase(username);
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

    close(server_fd);
    return 0;
}
