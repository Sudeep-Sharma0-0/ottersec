#include "control_server.hpp"
#include "telemetry.pb.h"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace ottersec {

ControlServer::ControlServer(int port)
    : port_(port), server_fd_(-1), client_fd_(-1), running_(false) {}

ControlServer::~ControlServer() { stop(); }

bool ControlServer::start() {
  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd_ < 0) {
    std::cerr << "[OtterSec Control] Failed to create socket.\n";
    return false;
  }

  int opt = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port_);

  if (bind(server_fd_, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    std::cerr << "[OtterSec Control] Bind failed on port " << port_ << ".\n";
    close(server_fd_);
    return false;
  }

  if (listen(server_fd_, 1) < 0) {
    std::cerr << "[OtterSec Control] Listen failed.\n";
    close(server_fd_);
    return false;
  }

  running_ = true;
  server_thread_ = std::thread(&ControlServer::accept_loop, this);
  std::cout << "[OtterSec Control] Listening on port " << port_ << "...\n";

  return true;
}

void ControlServer::stop() {
  running_ = false;
  if (server_fd_ >= 0) {
    shutdown(server_fd_, SHUT_RDWR);
    close(server_fd_);
    server_fd_ = -1;
  }
  if (client_fd_ >= 0) {
    close(client_fd_);
    client_fd_ = -1;
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
}

void ControlServer::accept_loop() {
  while (running_) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int new_client_fd =
        accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);

    if (new_client_fd >= 0) {
      char ip_str[INET_ADDRSTRLEN];
      inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);

      this->active_client_ip = std::string(ip_str);

      std::cout << "[OtterSec Control] Dynamic IP Discovery Success!\n";
      std::cout << "[OtterSec Control] Python backend connected from: "
                << this->active_client_ip << "\n";

      this->client_fd_ = new_client_fd;
    }
  }
}

void ControlServer::send_raw_payload(const uint8_t *data, size_t size) {
  if (client_fd_ < 0 || !data || size == 0)
    return;

  std::lock_guard<std::mutex> lock(send_mutex_);

  uint32_t net_length = htonl(static_cast<uint32_t>(size));
  if (send(client_fd_, &net_length, sizeof(net_length), MSG_NOSIGNAL) < 0) {
    close(client_fd_);
    client_fd_ = -1;
    return;
  }

  if (send(client_fd_, data, size, MSG_NOSIGNAL) < 0) {
    close(client_fd_);
    client_fd_ = -1;
  }
}
} // namespace ottersec
