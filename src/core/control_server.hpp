#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <string>
#include <thread>

namespace ottersec {

class ControlServer {
public:
  ControlServer(int port, const std::string &cert_path,
                const std::string &key_path);
  ~ControlServer();

  bool start();
  void stop();
  void send_raw_payload(const uint8_t *data, size_t size);

  std::string active_client_ip;

  std::string get_active_ip() {
    std::lock_guard<std::mutex> lock(send_mutex_);
    return active_client_ip;
  }

  std::atomic<bool> remote_resolve_triggered{false};

private:
  void accept_loop();
  SSL_CTX *create_ssl_context();

  int port_;
  std::string cert_path_;
  std::string key_path_;

  int server_fd_{-1};
  int client_fd_{-1};
  SSL_CTX *ssl_ctx_{nullptr};
  SSL *active_ssl_{nullptr};

  bool running_{false};
  std::thread server_thread_;
  std::mutex send_mutex_;
};

} // namespace ottersec
