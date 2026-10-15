#include "server/test_session.h"

#include <iostream>
#include <string>
#include <utility>

#include "common/error_codes.h"

namespace {
Message make_test_error(const std::string& request, const std::string& code,
                        const std::string& text) {
    return {MessageType::Error, {{"request", request}, {"code", code}, {"message", text}}};
}
}  // namespace

// 统计输出函数
void print_test_stats(const ServerTestState& state) {
    std::cout << "[server]\nrun_id=" << state.run_id << "\nexpected=" << state.expected_messages
              << "\nreceived=" << state.received_messages << "\nbytes=" << state.received_bytes
              << "\nsequence_errors=" << state.sequence_errors
              << "\ndata_errors=" << state.data_errors;

    if (state.received_messages <= state.expected_messages) {
        std::cout << "\nmissing=" << state.expected_messages - state.received_messages;
    } else {
        std::cout << " extra=" << state.received_messages - state.expected_messages;
    }
    std::cout << '\n';

    const double seconds = std::chrono::duration<double>(state.ended_at - state.started_at).count();
    const double messages_per_second = seconds > 0 ? state.received_messages / seconds : 0;
    const double mib_per_second =
        seconds > 0 ? state.received_bytes / seconds / (1024.0 * 1024.0) : 0;

    std::cout << "duration=" << seconds << " s"
              << "\nreceive_rate=" << messages_per_second << " msg/s"
              << "\nreceive_throughput=" << mib_per_second << " MiB/s\n";
}

// 接收循环内检查时间；无消息到达时不刷新。
void report_test_progress(ServerTestState& state) {
    const auto now = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(now - state.last_report_at).count();
    if (seconds < 1.0) {
        return;
    }

    const auto messages = state.received_messages - state.last_report_messages;
    const auto bytes = state.received_bytes - state.last_report_bytes;
    std::cout << "[server progress] run_id=" << state.run_id
              << " received=" << state.received_messages
              << " rate=" << messages / seconds << " msg/s"
              << " throughput=" << bytes / seconds / (1024.0 * 1024.0) << " MiB/s"
              << " sequence_errors=" << state.sequence_errors
              << " data_errors=" << state.data_errors << std::endl;

    state.last_report_at = now;
    state.last_report_messages = state.received_messages;
    state.last_report_bytes = state.received_bytes;
}

// 压力测试回显
Message handle_test(Message& request, ServerTestState& state) {
    // 确认测试状态
    if (!state.active) {
        return make_test_error("test", ErrorCode::InvalidArgument, "请先启动测试");
    }
    // 增加接收消息计数
    ++state.received_messages;

    const auto& payload = request.payload;
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

    // 记录是否出现数据异常
    if (data_error) {
        ++state.data_errors;
    }

    Message response;
    response.type = MessageType::Test;
    response.payload = std::move(request.payload);
    return response;
}

// 压力测试控制
Message handle_test_control(const Message& request, ServerTestState& state) {
    // 检测格式
    const auto& payload = request.payload;

    if (!payload.contains("phase") || !payload["phase"].is_string() ||
        !payload.contains("run_id") || !payload["run_id"].is_number_unsigned()) {
        return make_test_error("test_control", ErrorCode::InvalidArgument, "JSON格式错误");
    }
    const std::string phase = payload["phase"];
    const unsigned int run_id = payload["run_id"].get<unsigned int>();

    Message response;
    response.type = MessageType::TestControl;

    // 测试开始
    if (phase == "start") {
        if (state.active) {
            return make_test_error("test_control", ErrorCode::InvalidArgument, "当前测试未结束");
        }

        // 获取测试规模
        if (!payload.contains("message_count") || !payload["message_count"].is_number_unsigned() ||
            !payload.contains("data_bytes") || !payload["data_bytes"].is_number_unsigned()) {
            return make_test_error("test_control", ErrorCode::InvalidArgument, "JSON格式错误");
        }
        const auto count = payload["message_count"].get<std::uint64_t>();
        const auto bytes = payload["data_bytes"].get<std::uint64_t>();
        if (count == 0 || bytes == 0 || bytes > MAX_TEXT_SIZE) {
            return make_test_error("test_control", ErrorCode::InvalidArgument, "消息规模不合法");
        }

        // 重置统计量，设置本轮参数
        state = ServerTestState{};
        state.active = true;
        state.run_id = run_id;
        state.expected_messages = count;
        state.expected_data_bytes = bytes;
        state.started_at = std::chrono::steady_clock::now();
        state.last_report_at = state.started_at;

        // 返回响应
        response.payload = {{"phase", "start_ack"}, {"run_id", run_id}};
        return response;
    }

    // 测试结束
    if (phase == "end") {
        if (!state.active || run_id != state.run_id) {
            return make_test_error("test_control", ErrorCode::InvalidArgument, "没有对应测试");
        }

        state.ended_at = std::chrono::steady_clock::now();
        // 关闭统计更新，打印数据
        state.active = false;
        print_test_stats(state);

        response.payload = {{"phase", "end_ack"}, {"run_id", run_id}};
        return response;
    }
    return make_test_error("test_control", ErrorCode::InvalidArgument, "未知的测试阶段");
}
