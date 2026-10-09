#pragma once

// 错误码定义
namespace ErrorCode {

// 协议与格式错误
inline constexpr char InvalidFrame[] = "INVALID_FRAME";
inline constexpr char UnsupportedVersion[] = "UNSUPPORTED_VERSION";
inline constexpr char UnknownType[] = "UNKNOWN_TYPE";
inline constexpr char PayloadTooLarge[] = "PAYLOAD_TOO_LARGE";
inline constexpr char InvalidJson[] = "INVALID_JSON";

// 业务错误
inline constexpr char InvalidArgument[] = "INVALID_ARGUMENT";
inline constexpr char UnexpectedType[] = "UNEXPECTED_TYPE";
inline constexpr char NotLoggedIn[] = "NOT_LOGGED_IN";
inline constexpr char AlreadyLoggedIn[] = "ALREADY_LOGGED_IN";
inline constexpr char UsernameTaken[] = "USERNAME_TAKEN";
inline constexpr char UserOffline[] = "USER_OFFLINE";
inline constexpr char MessageTooLarge[] = "MESSAGE_TOO_LARGE";
inline constexpr char DeliveryFailed[] = "DELIVERY_FAILED";

}  // namespace ErrorCode
