/**
 * @file control_server.cpp
 * @brief Implements the Secure TLS Tripwire and Telemetry server.
 *
 * The ControlServer acts as the primary command-and-control channel between
 * the edge device and the backend. It waits for the Python backend to connect
 * over TLS, verifies an inner-tunnel authorization token, dynamically captures
 * the client's IP address, and streams serialized Protocol Buffer telemetry
 * data over the encrypted socket.
 */
#include "control_server.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace ottersec {

/**
 * @brief Constructs a new ControlServer with TLS certificates.
 *
 * @param port The TCP port to listen on.
 * @param cert_path Path to the public TLS certificate.
 * @param key_path Path to the private TLS key.
 */
ControlServer::ControlServer(int port, const std::string &cert_path,
                             const std::string &key_path)
    : port_(port), cert_path_(cert_path), key_path_(key_path) {
  ssl_ctx_ = create_ssl_context();
}

/**
 * @brief Destroys the ControlServer, ensuring all sockets and threads are
 * safely closed.
 */
ControlServer::~ControlServer() {
  stop();
  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
  }
}

/**
 * @brief Initializes OpenSSL context and loads certificates.
 */
SSL_CTX *ControlServer::create_ssl_context() {
  const SSL_METHOD *method = TLS_server_method();
  SSL_CTX *ctx = SSL_CTX_new(method);
  if (!ctx)
    return nullptr;

  if (SSL_CTX_use_certificate_file(ctx, cert_path_.c_str(), SSL_FILETYPE_PEM) <=
          0 ||
      SSL_CTX_use_PrivateKey_file(ctx, key_path_.c_str(), SSL_FILETYPE_PEM) <=
          0) {
    std::cerr << "[OtterSec Control] Failed to load TLS certificates for port "
              << port_ << "\n";
    SSL_CTX_free(ctx);
    return nullptr;
  }
  return ctx;
}

/**
 * @brief Initializes the TCP socket, binds to the port, and starts the listener
 * thread.
 */
bool ControlServer::start() {
  if (!ssl_ctx_)
    return false;

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
  std::cout << "[OtterSec Control] TLS Telemetry Server listening on port "
            << port_ << "...\n";

  return true;
}

/**
 * @brief Safely shuts down the server and clears OpenSSL resources.
 */
void ControlServer::stop() {
  running_ = false;
  if (server_fd_ >= 0) {
    shutdown(server_fd_, SHUT_RDWR);
    close(server_fd_);
    server_fd_ = -1;
  }
  {
    std::lock_guard<std::mutex> lock(send_mutex_);
    if (active_ssl_) {
      SSL_shutdown(active_ssl_);
      SSL_free(active_ssl_);
      active_ssl_ = nullptr;
    }
    if (client_fd_ >= 0) {
      close(client_fd_);
      client_fd_ = -1;
    }
  }
  if (server_thread_.joinable()) {
    server_thread_.join();
  }
}

/**
 * @brief Background thread loop that accepts connections and enforces TLS +
 * Token Auth.
 */
void ControlServer::accept_loop() {
  while (running_) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);

    int new_fd =
        accept(server_fd_, (struct sockaddr *)&client_addr, &client_len);
    if (new_fd < 0)
      continue;

    SSL *ssl = SSL_new(ssl_ctx_);
    SSL_set_fd(ssl, new_fd);

    if (SSL_accept(ssl) <= 0) {
      std::cerr
          << "[OtterSec Control] TLS Handshake failed. Dropping connection.\n";
      SSL_free(ssl);
      close(new_fd);
      continue;
    }

    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    char auth_buf[32] = {0};
    int bytes = SSL_read(ssl, auth_buf, 17);

    const std::string EXPECTED_TOKEN = "OTTER_ADMIN_TOKEN";
    const std::string RESOLVE_TOKEN = "OTTER_RESOLVE_CMD";

    if (bytes == RESOLVE_TOKEN.length() &&
        memcmp(auth_buf, RESOLVE_TOKEN.data(), bytes) == 0) {
      std::cout
          << "\n[OtterSec] Received REMOTE RESOLVE Command from Backend!\n";
      remote_resolve_triggered = true;
      SSL_shutdown(ssl);
      SSL_free(ssl);
      close(new_fd);
      continue;
    }

    if (bytes != EXPECTED_TOKEN.length() ||
        memcmp(auth_buf, EXPECTED_TOKEN.data(), bytes) != 0) {
      std::cerr << "[OtterSec Control] Unauthorized client failed token "
                   "verification. Dropping.\n";
      SSL_shutdown(ssl);
      SSL_free(ssl);
      close(new_fd);
      continue;
    }

    tv.tv_sec = 0;
    setsockopt(new_fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);

    std::lock_guard<std::mutex> lock(send_mutex_);
    if (active_ssl_) {
      SSL_shutdown(active_ssl_);
      SSL_free(active_ssl_);
      close(client_fd_);
    }

    this->active_client_ip = std::string(ip_str);
    this->client_fd_ = new_fd;
    this->active_ssl_ = ssl;

    std::cout << "[OtterSec Control] TLS Client Verified & Connected from: "
              << this->active_client_ip << "\n";
  }
}

/**
 * @brief Transmits a length-prefixed raw binary payload over TLS.
 */
void ControlServer::send_raw_payload(const uint8_t *data, size_t size) {
  std::lock_guard<std::mutex> lock(send_mutex_);
  if (!active_ssl_ || !data || size == 0)
    return;

  uint32_t net_length = htonl(static_cast<uint32_t>(size));

  // Send 4-byte length prefix
  if (SSL_write(active_ssl_, &net_length, sizeof(net_length)) <= 0) {
    SSL_free(active_ssl_);
    active_ssl_ = nullptr;
    close(client_fd_);
    client_fd_ = -1;
    return;
  }

  // Send Protobuf payload
  if (SSL_write(active_ssl_, data, size) <= 0) {
    SSL_free(active_ssl_);
    active_ssl_ = nullptr;
    close(client_fd_);
    client_fd_ = -1;
  }
}

} // namespace ottersec
