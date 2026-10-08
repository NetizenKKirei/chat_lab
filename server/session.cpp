#include "server/session.h"

#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/error_codes.h"
#include "common/protocol.h"
#include "server/connection.h"

std::unordered_map<std::string, std::shared_ptr<Connection>> online_users;  // 在线用户列表
std::mutex users_mutex;                                                     // 线程加锁

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
Message handle_login(const Message& request, std::string& username,
                     const std::shared_ptr<Connection>& connection) {
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
        inserted = online_users.emplace(requested_name, connection).second;
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
        for (const auto& entry : online_users) {
            users.push_back(entry.first);
        }
    }
    std::sort(users.begin(), users.end());

    // 返回响应
    Message response;
    response.type = MessageType::Response;
    response.payload = {{"request", "list"}, {"users", users}};
    return response;
}

// 群聊函数
Message handle_broadcast(const Message& request, const std::string username) {
    // 检查请求是否合法
    if (username.empty()) {
        return make_error("broadcast", ErrorCode::NotLoggedIn, "请先登录");
    }
    if (!request.payload.contains("text") || !request.payload["text"].is_string()) {
        return make_error("broadcast", ErrorCode::InvalidArgument, "text必须为字符串");
    }
    std::string text = request.payload["text"].get<std::string>();

    if (text.empty()) {
        return make_error("broadcast", ErrorCode::InvalidArgument, "正文不能为空");
    }
    if (text.size() > MAX_TEXT_SIZE) {
        return make_error("broadcast", ErrorCode::MessageTooLarge, "正文超过1MiB");
    }

    // 准备要群发的消息
    Message delivery;
    delivery.type = MessageType::Broadcast;
    delivery.payload = {{"from", username}, {"text", text}};

    // 加锁获取群发列表(在线connection的智能指针)
    std::vector<std::shared_ptr<Connection>> targets;
    {
        std::lock_guard<std::mutex> lock(users_mutex);
        for (const auto& entry : online_users) {
            if (entry.first != username) {
                targets.push_back(entry.second);
            }
        }
    }
    if (targets.empty()) {
        return make_error("broadcast", ErrorCode::DeliveryFailed, "当前无其他在线用户");
    }

    // 发送消息
    bool failed = false;
    for (const auto& target : targets) {
        if (!target->send(delivery)) {
            failed = true;
            ::shutdown(target->fd, SHUT_RDWR);
        }
    }

    // 返回响应
    if (failed) {
        return make_error("broadcast", ErrorCode::DeliveryFailed, "部分消息发送失败");
    }
    Message response;
    response.type = MessageType::Response;
    response.payload = {{"request", "broadcast"}};
    return response;
}

// 单客户端处理
void handle_client(int client_fd) {
    auto connection = std::make_shared<Connection>(
        client_fd);  // 创建Connection对象，传入client_fd给构造函数并返回共享智能指针
    std::string username;

    // 消息接收循环
    while (true) {
        Message request;
        if (!recv_message(connection->fd, request)) {
            break;
        }
        std::cout << "消息类型：" << static_cast<unsigned int>(request.type)
                  << "\n消息内容: " << request.payload.dump() << "\n";

        Message response;
        if (request.type == MessageType::Login) {
            response = handle_login(request, username, connection);
        } else if (request.type == MessageType::List) {
            response = handle_list(username);
        } else if (request.type == MessageType::Broadcast) {
            response = handle_broadcast(request, username);
        } else {
            response.type = MessageType::Error;
            response.payload = {{"request", "unknown"},
                                {"code", ErrorCode::UnexpectedType},
                                {"message", "该请求暂未实现"}};
        }
        if (!connection->send(response)) {
            break;
        }
    }
    // 客户端关闭前释放用户名
    if (!username.empty()) {
        std::lock_guard<std::mutex> lock(users_mutex);
        // 确认当前用户名在在线用户列表中
        auto it = online_users.find(username);
        if (it != online_users.end() && it->second == connection) {
            online_users.erase(it);
        }
    }
    ::shutdown(connection->fd, SHUT_RDWR);
}
