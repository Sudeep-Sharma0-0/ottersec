#include "handshake_server.hpp"
#include <iostream>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ottersec {

HandshakeServer::HandshakeServer(uint16_t port, const std::string &cert_path,
                                 const std::string &key_path)
    : port_(port), cert_path_(cert_path), key_path_(key_path) {
  ssl_ctx_ = create_ssl_context();
}

HandshakeServer::~HandshakeServer() {
  stop();
  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
  }
}

SSL_CTX *HandshakeServer::create_ssl_context() {
  const SSL_METHOD *method = TLS_server_method();
  SSL_CTX *ctx = SSL_CTX_new(method);
  if (!ctx)
    return nullptr;

  if (SSL_CTX_use_certificate_file(ctx, cert_path_.c_str(), SSL_FILETYPE_PEM) <=
          0 ||
      SSL_CTX_use_PrivateKey_file(ctx, key_path_.c_str(), SSL_FILETYPE_PEM) <=
          0) {
    std::cerr << "[OtterSec] Failed to load TLS certificates.\n";
    SSL_CTX_free(ctx);
    return nullptr;
  }
  return ctx;
}

void HandshakeServer::start(OnCompleteCb callback) {
  on_complete_ = callback;
  running_ = true;
  listener_thread_ = std::thread(&HandshakeServer::listen_loop, this);
}

void HandshakeServer::stop() {
  running_ = false;
  if (server_fd_ >= 0) {
    shutdown(server_fd_, SHUT_RDWR);
    close(server_fd_);
    server_fd_ = -1;
  }
  if (listener_thread_.joinable()) {
    listener_thread_.join();
  }
}

void HandshakeServer::listen_loop() {
  if (!ssl_ctx_) {
    if (on_complete_)
      on_complete_(false, {}, {});
    return;
  }

  server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  int opt = 1;
  setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt,
             sizeof(opt));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port_);

  if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0 ||
      listen(server_fd_, 1) < 0) {
    running_ = false;
    return;
  }

  std::cout << "[OtterSec] TLS Handshake Server listening on port " << port_
            << "...\n";

  while (running_) {
    int client_fd = accept(server_fd_, nullptr, nullptr);
    if (client_fd < 0)
      continue;

    SSL *ssl = SSL_new(ssl_ctx_);
    SSL_set_fd(ssl, client_fd);

    if (SSL_accept(ssl) <= 0) {
      std::cerr << "[OtterSec] TLS Handshake failed.\n";
      ERR_print_errors_fp(stderr);
    } else {
      std::cout
          << "[OtterSec] TLS Connection established. Generating SRTP keys...\n";

      std::vector<uint8_t> master_key(16);
      std::vector<uint8_t> master_salt(14);

      RAND_bytes(master_key.data(), master_key.size());
      RAND_bytes(master_salt.data(), master_salt.size());

      std::vector<uint8_t> payload;
      payload.insert(payload.end(), master_key.begin(), master_key.end());
      payload.insert(payload.end(), master_salt.begin(), master_salt.end());

      int bytes_written = SSL_write(ssl, payload.data(), payload.size());
      if (bytes_written <= 0) {
        std::cerr << "[OtterSec] Error: Failed to transmit keys to backend.\n";
      } else {
        std::cout << "[OtterSec] " << bytes_written
                  << " bytes of key material transmitted securely.\n";
      }

      if (on_complete_)
        on_complete_(true, master_key, master_salt);

      if (on_complete_)
        on_complete_(true, master_key, master_salt);

      SSL_shutdown(ssl);
      SSL_free(ssl);
      close(client_fd);
      break;
    }
    SSL_free(ssl);
    close(client_fd);
  }
  running_ = false;
}
} // namespace ottersec
