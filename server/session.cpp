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

std::unordered_map<std::string, std::shared_ptr<Connection>>
    online_users;        // 在线用户列表（用户名-连接对象键值对）
std::mutex users_mutex;  // 线程加锁

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
Message handle_broadcast(const Message& request, const std::string& username) {
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

    // 准备要群发的消息
    Message delivery;
    delivery.type = MessageType::Broadcast;
    delivery.payload = {{"from", username}, {"text", text}};

    // 发送消息
    bool failed = false;
    for (const auto& target : targets) {
        MessageStatus status = target->send(delivery);
        if (status != MessageStatus::Success) {
            if (status == MessageStatus::Close) {
                ::shutdown(target->fd, SHUT_RDWR);
            }
            failed = true;
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

// 私聊函数
Message handle_private(const Message& request, const std::string& username) {
    // 检查请求是否合法
    if (username.empty()) {
        return make_error("private", ErrorCode::NotLoggedIn, "请先登录");
    }
    if (!request.payload.contains("to") || !request.payload["to"].is_string() ||
        !request.payload.contains("text") || !request.payload["text"].is_string()) {
        return make_error("private", ErrorCode::InvalidArgument, "to/text必须为字符串");
    }

    std::string receiver = request.payload["to"].get<std::string>();
    std::string text = request.payload["text"].get<std::string>();

    if (!valid_username(receiver)) {
        return make_error("private", ErrorCode::InvalidArgument, "用户名不合法");
    }
    if (text.empty()) {
        return make_error("private", ErrorCode::InvalidArgument, "正文不能为空");
    }
    if (text.size() > MAX_TEXT_SIZE) {
        return make_error("private", ErrorCode::MessageTooLarge, "正文超过1MiB");
    }

    // 加锁查询私聊对象(在线connection的智能指针)
    std::shared_ptr<Connection> target;
    {
        std::lock_guard<std::mutex> lock(users_mutex);
        auto it = online_users.find(receiver);
        if (it != online_users.end()) {
            target = it->second;
        }
    }
    if (!target) {
        return make_error("private", ErrorCode::UserOffline, "目标用户不在线");
    }

    // 准备要私发的消息
    Message delivery;
    delivery.type = MessageType::Private;
    delivery.payload = {{"from", username}, {"text", text}};

    // 发送消息
    MessageStatus status = target->send(delivery);

    if (status != MessageStatus::Success) {
        if (status == MessageStatus::Close) {
            ::shutdown(target->fd, SHUT_RDWR);
        }
        return make_error("private", ErrorCode::DeliveryFailed, "私聊发送失败");
    }

    Message response;
    response.type = MessageType::Response;
    response.payload = {{"request", "private"}};
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
        // 判断接收状态
        MessageStatus status = recv_message(connection->fd, request);
        if (status == MessageStatus::Close) {
            break;
        }
        if (status == MessageStatus::Rejected) {
            Message error = make_error("unknown", ErrorCode::InvalidJson, "负载不合法或解析失败");
            if (connection->send(error) != MessageStatus::Success) {
                std::cerr << "服务器响应失败\n";
                break;
            }
            continue;
        }
        std::cout << "消息类型：" << static_cast<unsigned int>(request.type)
                  << "\n消息内容: " << request.payload.dump() << "\n";

        // 分类型处理消息
        Message response;
        if (request.type == MessageType::Login) {
            response = handle_login(request, username, connection);
        } else if (request.type == MessageType::List) {
            response = handle_list(username);
        } else if (request.type == MessageType::Broadcast) {
            response = handle_broadcast(request, username);
        } else if (request.type == MessageType::Private) {
            response = handle_private(request, username);
        } else {
            response.type = MessageType::Error;
            response.payload = {{"request", "unknown"},
                                {"code", ErrorCode::UnexpectedType},
                                {"message", "该请求暂未实现"}};
        }
        if (connection->send(response) != MessageStatus::Success) {
            std::cerr << "服务器响应失败\n";
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
