/**
 * @file handshake_server.cpp
 * @brief Implements the TLS Handshake server for secure key exchange.
 *
 * The HandshakeServer listens on a dedicated TCP port to authenticate the
 * Python backend. Once the TLS handshake succeeds, it generates
 * cryptographically secure random bytes (AES key and salt) for the GStreamer
 * SRTP pipeline, securely transmits them to the client, and fires a callback to
 * advance the SessionManager's state.
 */
#include "handshake_server.hpp"
#include <iostream>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ottersec {

/**
 * @brief Constructs a new HandshakeServer object.
 *
 * Initializes the TLS context using the provided certificate and private key.
 *
 * @param port The TCP port to listen on for TLS connections.
 * @param cert_path Path to the server's public certificate file (PEM format).
 * @param key_path Path to the server's private key file (PEM format).
 */
HandshakeServer::HandshakeServer(uint16_t port, const std::string &cert_path,
                                 const std::string &key_path)
    : port_(port), cert_path_(cert_path), key_path_(key_path) {
  ssl_ctx_ = create_ssl_context();
}

/**
 * @brief Destroys the HandshakeServer, ensuring sockets are closed
 *        and the OpenSSL context is freed.
 */
HandshakeServer::~HandshakeServer() {
  stop();
  if (ssl_ctx_) {
    SSL_CTX_free(ssl_ctx_);
  }
}

/**
 * @brief Creates and configures the OpenSSL TLS context.
 *
 * @return SSL_CTX* Pointer to the configured OpenSSL context, or nullptr
 *         if context creation or certificate loading fails.
 */
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

/**
 * @brief Starts the asynchronous TLS listening loop in a detached thread.
 *
 * @param callback The function to invoke once the handshake succeeds and
 *                 keys are generated, or if the server encounters a fatal
 * error.
 */
void HandshakeServer::start(OnCompleteCb callback) {
  on_complete_ = callback;
  running_ = true;
  listener_thread_ = std::thread(&HandshakeServer::listen_loop, this);
}

/**
 * @brief Forces the listener thread to shut down and close active sockets.
 */
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

/**
 * @brief The core thread function that handles incoming TLS connections.
 *
 * Binds to the specified TCP port and waits for the Python backend. Upon
 * connection, it performs the TLS handshake. If successful, it uses OpenSSL's
 * RAND_bytes to generate a 16-byte SRTP key and a 14-byte SRTP salt, transmits
 * them over the encrypted tunnel, and fires the completion callback.
 */
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

      // Allocate precise byte lengths mandated by AES-128-ICM
      std::vector<uint8_t> master_key(16);
      std::vector<uint8_t> master_salt(14);

      // Generate cryptographically secure random numbers
      RAND_bytes(master_key.data(), master_key.size());
      RAND_bytes(master_salt.data(), master_salt.size());

      // Serialize into a flat 30-byte payload for transmission
      std::vector<uint8_t> payload;
      payload.insert(payload.end(), master_key.begin(), master_key.end());
      payload.insert(payload.end(), master_salt.begin(), master_salt.end());

      // Transmit the crypto material over the secure TLS socket
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
