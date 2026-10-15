#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <iostream>
#include <mutex>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <thread>

#include "common/protocol.h"
#include "common/unique_fd.h"

// 当前测试阶段枚举
enum class ClientPhase { WaitingStartAck, Running, WaitingEndAck, Finished, Failed };

// 测试消息状态
struct TestRunState {
    std::mutex mutex;
    std::condition_variable changed;

    ClientPhase phase = ClientPhase::WaitingStartAck;
    std::string error;

    unsigned int run_id = 0;
    std::uint64_t expected_messages = 0;
    std::size_t expected_data_bytes = 0;

    std::uint64_t target_bytes_per_second = 0;
    std::uint64_t sent_messages = 0;
    std::uint64_t rejected_messages = 0;
    double send_seconds = 0;

    std::uint64_t received_messages = 0;
    std::uint64_t received_bytes = 0;
    std::uint64_t next_expected_sequence = 0;
    std::uint64_t sequence_errors = 0;
    std::uint64_t data_errors = 0;

    std::chrono::steady_clock::time_point started_at;
    std::chrono::steady_clock::time_point ended_at;

    std::chrono::steady_clock::time_point last_report_at;
    std::uint64_t last_report_messages = 0;
    std::uint64_t last_report_bytes = 0;
};

// 统计输出函数：接收线程结束后调用，或调用者持有state.mutex。
void print_test_stats(const TestRunState& state) {
    std::cout << "[client]\nrun_id=" << state.run_id << "\nexpected=" << state.expected_messages
              << "\nreceived=" << state.received_messages << "\nbytes=" << state.received_bytes
              << "\nsequence_errors=" << state.sequence_errors
              << "\ndata_errors=" << state.data_errors;

    if (state.received_messages <= state.expected_messages) {
        std::cout << "\nmissing=" << state.expected_messages - state.received_messages;
    } else {
        std::cout << "\nextra=" << state.received_messages - state.expected_messages;
    }
    std::cout << '\n';

    const double seconds = std::chrono::duration<double>(state.ended_at - state.started_at).count();
    const double messages_per_second = seconds > 0 ? state.received_messages / seconds : 0;
    const double mib_per_second =
        seconds > 0 ? state.received_bytes / seconds / (1024.0 * 1024.0) : 0;

    std::cout << "duration=" << seconds << " s"
              << "\nreceive_rate=" << messages_per_second << " msg/s"
              << "\nreceive_throughput=" << mib_per_second << " MiB/s\n";

    const double send_rate = state.send_seconds > 0 ? state.sent_messages / state.send_seconds : 0;
    const double send_throughput = state.send_seconds > 0
                                       ? state.sent_messages / state.send_seconds *
                                             state.expected_data_bytes / (1024.0 * 1024.0)
                                       : 0;

    std::cout << "sent=" << state.sent_messages << "\nrejected=" << state.rejected_messages
              << "\nsend_duration=" << state.send_seconds << " s"
              << "\nsend_rate=" << send_rate << " msg/s"
              << "\ntarget=" << state.target_bytes_per_second / (1024.0 * 1024.0) << " MiB/s"
              << "\nactual_send=" << send_throughput << " MiB/s\n";
}

// 调用者持有state.mutex；接收循环内检查时间，无消息到达时不刷新。
void report_test_progress(TestRunState& state) {
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - state.last_report_at).count();
    if (seconds < 1.0) {
        return;
    }

    const auto messages = state.received_messages - state.last_report_messages;
    const auto bytes = state.received_bytes - state.last_report_bytes;
    std::cout << "[client progress] run_id=" << state.run_id
              << " received=" << state.received_messages << " rate=" << messages / seconds
              << " msg/s"
              << " throughput=" << bytes / seconds / (1024.0 * 1024.0) << " MiB/s"
              << " sequence_errors=" << state.sequence_errors
              << " data_errors=" << state.data_errors << std::endl;

    state.last_report_at = now;
    state.last_report_messages = state.received_messages;
    state.last_report_bytes = state.received_bytes;
}

// 失败处理函数
void fail_test(TestRunState& state, const std::string& error) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);

        if (state.phase == ClientPhase::Finished || state.phase == ClientPhase::Failed) {
            return;
        }

        state.error = error;
        state.phase = ClientPhase::Failed;
    }

    state.changed.notify_all();
}

// 控制消息处理
bool handle_control_reply(const Message& message, TestRunState& state) {
    const auto& payload = message.payload;

    if (!payload.contains("phase") || !payload["phase"].is_string() ||
        !payload.contains("run_id") || !payload["run_id"].is_number_unsigned()) {
        return false;
    }

    if (payload["run_id"].get<unsigned int>() != state.run_id) {
        return false;
    }

    if (payload["phase"] == "start_ack" && state.phase == ClientPhase::WaitingStartAck) {
        state.started_at = std::chrono::steady_clock::now();
        state.last_report_at = state.started_at;
        state.phase = ClientPhase::Running;
        return true;
    }

    if (payload["phase"] == "end_ack" && state.phase == ClientPhase::WaitingEndAck) {
        state.ended_at = std::chrono::steady_clock::now();
        state.phase = ClientPhase::Finished;
        return true;
    }

    return false;
}

// 子线程函数：接收服务端消息
void receive_messages(int socket_fd, TestRunState& state) {
    while (true) {
        Message message;
        MessageStatus status = recv_message(socket_fd, message);
        // 查看消息状态
        if (status == MessageStatus::Close) {
            fail_test(state, "连接中断");
            return;
        }
        if (status == MessageStatus::Rejected) {
            std::lock_guard<std::mutex> lock(state.mutex);
            ++state.data_errors;
            if (state.phase == ClientPhase::Running || state.phase == ClientPhase::WaitingEndAck) {
                report_test_progress(state);
            }
            continue;
        }
        // 分类型处理消息
        if (message.type == MessageType::Error) {
            fail_test(state, "服务端返回错误：" + message.payload.dump());
            return;
        }
        // 加锁处理控制消息
        if (message.type == MessageType::TestControl) {
            bool valid;
            bool finished;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if (state.phase == ClientPhase::Failed) {
                    return;
                }
                valid = handle_control_reply(message, state);
                finished = (state.phase == ClientPhase::Finished);
            }
            if (!valid) {
                fail_test(state, "控制响应与当前测试不匹配");
                return;
            }
            state.changed.notify_all();
            if (finished) {
                return;
            }
            continue;
        }

        // 接收测试消息
        if (message.type == MessageType::Test) {
            std::lock_guard<std::mutex> lock(state.mutex);
            if (state.phase == ClientPhase::Failed) {
                return;
            }

            // 增加消息计数
            ++state.received_messages;
            const auto& payload = message.payload;
            bool data_error = false;

            // 检查序列编号
            if (payload.contains("sequence") && payload["sequence"].is_number_unsigned()) {
                const auto sequence = payload["sequence"].get<std::uint64_t>();
                if (sequence != state.next_expected_sequence) {
                    ++state.sequence_errors;
                }
                state.next_expected_sequence = sequence + 1;
            } else {
                data_error = true;
            }

            // 检查消息长度和内容
            if (payload.contains("data") && payload["data"].is_string()) {
                const auto& data = payload["data"].get_ref<const std::string&>();

                state.received_bytes += data.size();

                if (data.size() != state.expected_data_bytes ||
                    data.find_first_not_of('x') != std::string::npos) {
                    data_error = true;
                }
            } else {
                data_error = true;
            }
            if (data_error) {
                ++state.data_errors;
            }
            report_test_progress(state);
            continue;
        }

        fail_test(state, "收到非测试消息");
        return;
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

int run_test(const std::string& host, const std::string& port,
             unsigned int throughput, unsigned int run_id,
             std::size_t data_bytes, unsigned int duration_seconds);

int main(int argc, char* argv[]) {
    // 设置启动参数
    std::string host = "127.0.0.1";
    std::string port = "9000";
    unsigned int throughput = 10;

    if (argc >= 2) {
        throughput = std::stoul(argv[1]);
    }
    if (argc >= 3) {
        host = argv[2];
    }
    if (argc >= 4) {
        port = argv[3];
    }
    if (argc > 4) {
        std::cerr << "参数过多：" << argv[0] << "[目标吞吐量] [主机名或IP] [端口]\n";
        return 1;
    }
    std::cout << "目标服务器：" << host << ":" << port << '\n';

    // 设置目标吞吐率
    if (throughput == 0) {
        std::cerr << "目标吞吐量必须大于0\n";
        return 1;
    }
    std::cout << "\n=== 大消息测试：1 MiB/条 ===" << std::endl;
    const int large_result = run_test(host, port, throughput, 0, 1024 * 1024, 10);

    std::this_thread::sleep_for(std::chrono::seconds(1));

    std::cout << "\n=== 小消息测试：1 KiB/条 ===" << std::endl;
    const int small_result = run_test(host, port, throughput, 1, 1024, 10);

    return large_result != 0 || small_result != 0 ? 1 : 0;
}

// 每轮独立连接、独立状态，沿用相同的收发与统计流程。
int run_test(const std::string& host, const std::string& port,
             unsigned int throughput, unsigned int run_id,
             std::size_t data_bytes, unsigned int duration_seconds) {
    const std::uint64_t target_bytes_per_second =
        static_cast<std::uint64_t>(throughput) * 1024 * 1024;

    // 创建测试socket并连接服务器
    UniqueFd socket_fd(connect_to_server(host, port));
    if (socket_fd.get() == -1) {
        return 1;
    }
    std::cout << "已连接服务器" << std::endl;

    // 定义消息状态
    TestRunState state;
    state.run_id = run_id;
    state.target_bytes_per_second = target_bytes_per_second;
    state.expected_data_bytes = data_bytes;
    state.expected_messages =
        target_bytes_per_second * duration_seconds / state.expected_data_bytes;

    // 创建接收线程
    std::thread receiver(receive_messages, socket_fd.get(), std::ref(state));

    // 创建测试开始消息
    Message start;
    start.type = MessageType::TestControl;
    start.payload = {{"phase", "start"},
                     {"run_id", state.run_id},
                     {"message_count", state.expected_messages},
                     {"data_bytes", state.expected_data_bytes}};

    // 发送start
    MessageStatus status = send_message(socket_fd.get(), start);
    if (status != MessageStatus::Success) {
        fail_test(state, "发送启动请求失败");
    }

    // 等待接收服务端回显
    {
        std::unique_lock<std::mutex> lock(state.mutex);

        state.changed.wait(lock, [&] {
            return state.phase == ClientPhase::Running || state.phase == ClientPhase::Failed;
        });
        if (state.phase == ClientPhase::Failed) {
            std::cerr << state.error << '\n';

            lock.unlock();
            shutdown(socket_fd.get(), SHUT_RDWR);
            receiver.join();
            return 1;
        }
    }

    // 发送测试消息
    Message test;
    test.type = MessageType::Test;
    test.payload = {{"sequence", std::uint64_t{0}},
                    {"data", std::string(state.expected_data_bytes, 'x')}};

    // 计算发送间隔
    const auto started_at = std::chrono::steady_clock::now();
    const double interval_seconds = static_cast<double>(state.expected_data_bytes) /
                                    static_cast<double>(target_bytes_per_second);

    // 发送计数仅由主线程更新，循环结束后统一写回。
    std::uint64_t sent_messages = 0;
    std::uint64_t rejected_messages = 0;

    for (std::uint64_t i = 0; i < state.expected_messages; ++i) {
        // 按间隔发送消息
        std::this_thread::sleep_until(started_at +
                                      std::chrono::duration<double>(i * interval_seconds));
        test.payload["sequence"] = i;

        status = send_message(socket_fd.get(), test);

        if (status == MessageStatus::Rejected) {
            ++rejected_messages;
            continue;
        }
        if (status == MessageStatus::Close) {
            fail_test(state, "发送Test失败");
            shutdown(socket_fd.get(), SHUT_RDWR);
            receiver.join();
            return 1;
        }
        ++sent_messages;
    }

    // 记录发送耗时，速率统一在统计输出函数中计算。
    const auto send_ended_at = std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.sent_messages = sent_messages;
        state.rejected_messages = rejected_messages;
        state.send_seconds = std::chrono::duration<double>(send_ended_at - started_at).count();
    }

    // 创建测试结束消息
    Message end;
    end.type = MessageType::TestControl;
    end.payload = {{"phase", "end"}, {"run_id", state.run_id}};

    // 设置阶段
    {
        std::unique_lock<std::mutex> lock(state.mutex);

        if (state.phase == ClientPhase::Failed) {
            std::cerr << state.error << '\n';
            lock.unlock();
            shutdown(socket_fd.get(), SHUT_RDWR);
            receiver.join();
            return 1;
        }

        state.phase = ClientPhase::WaitingEndAck;
    }

    // 发送end
    status = send_message(socket_fd.get(), end);
    if (status != MessageStatus::Success) {
        fail_test(state, "发送结束请求失败");
    }

    // 等待服务端结束确认
    {
        std::unique_lock<std::mutex> lock(state.mutex);

        state.changed.wait(lock, [&] {
            return state.phase == ClientPhase::Finished || state.phase == ClientPhase::Failed;
        });

        if (state.phase == ClientPhase::Failed) {
            std::cerr << state.error << '\n';

            lock.unlock();
            shutdown(socket_fd.get(), SHUT_RDWR);
            receiver.join();
            return 1;
        }
    }

    // 测试结束
    shutdown(socket_fd.get(), SHUT_RDWR);
    receiver.join();
    std::cout << "本轮测试完成\n";
    print_test_stats(state);
    return 0;
}
