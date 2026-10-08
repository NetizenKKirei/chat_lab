#pragma once

// 处理一个客户端连接，并在结束时关闭 client_fd。
void handle_client(int client_fd);
