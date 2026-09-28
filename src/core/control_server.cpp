/**
 * @file control_server.cpp
 * @brief Implements the Silent Tripwire and Telemetry TCP server.
 *
 * The ControlServer acts as the primary command-and-control channel between
 * the edge device and the backend. It waits for the Python backend to connect,
 * dynamically captures the client's IP address (to route the UDP video later),
 * and streams serialized Protocol Buffer telemetry data (bounding boxes,
 * keypoints, fall events) over a persistent TCP socket.
 */
#include "control_server.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>

namespace ottersec {

/**
 * @brief Constructs a new ControlServer.
 *
 * @param port The TCP port to listen on for incoming control connections
 * (default 8080).
 */
ControlServer::ControlServer(int port)
    : port_(port), server_fd_(-1), client_fd_(-1), running_(false) {}

/**
 * @brief Destroys the ControlServer, ensuring all sockets and threads are
 * safely closed.
 */
ControlServer::~ControlServer() { stop(); }

/**
 * @brief Initializes the TCP socket, binds to the port, and starts the listener
 * thread.
 *
 * Enables SO_REUSEADDR to prevent "Address already in use" errors during rapid
 * restarts. Spawns an asynchronous thread running `accept_loop`.
 *
 * @return true if the socket successfully binds and the listener thread starts.
 * @return false if socket creation, binding, or listening fails.
 */
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

/**
 * @brief Safely shuts down the server.
 *
 * Closes the active client connection, shuts down the main listening socket,
 * and joins the `accept_loop` thread back to the main process to prevent
 * segmentation faults.
 */
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

/**
 * @brief Background thread loop that accepts incoming client connections.
 *
 * Upon a successful connection, it extracts the client's IPv4 address using
 * inet_ntop and stores it in `active_client_ip`. This dynamic discovery is
 * critical for allowing the SessionManager to know where to shoot the UDP
 * video.
 */
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

/**
 * @brief Transmits a length-prefixed raw binary payload to the connected
 * client.
 *
 * Uses a standard network framing protocol: it first sends a 4-byte header
 * (converted to network byte order via `htonl`) indicating the exact size
 * of the payload, followed by the payload itself. Uses MSG_NOSIGNAL to
 * prevent the application from crashing via SIGPIPE if the client disconnects.
 *
 * @param data Pointer to the serialized Protobuf byte array.
 * @param size The total number of bytes in the payload.
 */
void ControlServer::send_raw_payload(const uint8_t *data, size_t size) {
  if (client_fd_ < 0 || !data || size == 0)
    return;

  std::lock_guard<std::mutex> lock(send_mutex_);

  // Send 4-byte length prefix (Network Byte Order)
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
