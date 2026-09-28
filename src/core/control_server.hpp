#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace ottersec {

class ControlServer {
public:
  ControlServer(int port = 8080);
  ~ControlServer();

  bool start();

  void stop();

  void send_raw_payload(const uint8_t *data, size_t size);

  std::string active_client_ip = "";

private:
  void accept_loop();

  int port_;
  int server_fd_;
  std::atomic<int> client_fd_;
  std::atomic<bool> running_;

  std::thread server_thread_;
  std::mutex send_mutex_;
};

} // namespace ottersec
