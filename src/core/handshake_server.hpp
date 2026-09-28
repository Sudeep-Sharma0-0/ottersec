#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

typedef struct ssl_ctx_st SSL_CTX;

namespace ottersec {
class HandshakeServer {
public:
  using OnCompleteCb =
      std::function<void(bool success, const std::vector<uint8_t> &key,
                         const std::vector<uint8_t> &salt)>;

  HandshakeServer(uint16_t port, const std::string &cert_path,
                  const std::string &key_path);
  ~HandshakeServer();

  void start(OnCompleteCb callback);
  void stop();

private:
  void listen_loop();
  SSL_CTX *create_ssl_context();

  uint16_t port_;
  std::string cert_path_;
  std::string key_path_;

  std::atomic<bool> running_{false};
  int server_fd_{-1};
  SSL_CTX *ssl_ctx_{nullptr};

  std::thread listener_thread_;
  OnCompleteCb on_complete_;
};
} // namespace ottersec
