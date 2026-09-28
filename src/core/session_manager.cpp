#include "session_manager.hpp"
#include <iostream>

namespace ottersec {

SessionManager::SessionManager(const OtterHandshakeConfig *config) {
  handshake_port_ = config ? config->handshake_port : 8443;

  cert_path_ = (config && config->cert_path) ? config->cert_path : "server.crt";
  key_path_ = (config && config->key_path) ? config->key_path : "server.key";
}

SessionManager::~SessionManager() { shutdown(); }

bool SessionManager::trigger_fall_event(const OtterFallEvent *event) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (current_state_ != State::IDLE)
    return false;

  std::cout << "[OtterSec] Fall Detected! Triggering Handshake.\n";
  transition_to(State::HANDSHAKING);

  server_ =
      std::make_unique<HandshakeServer>(handshake_port_, cert_path_, key_path_);

  server_->start([this](bool success, const std::vector<uint8_t> &key,
                        const std::vector<uint8_t> &salt) {
    this->on_handshake_complete(success, key, salt);
  });

  return true;
}

void SessionManager::on_handshake_complete(bool success,
                                           const std::vector<uint8_t> &key,
                                           const std::vector<uint8_t> &salt) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (success) {
    std::cout << "[OtterSec] Handshake Success. Securing keys in memory.\n";
    sec_context_.set_keys(key.data(), key.size(), salt.data(), salt.size());

    pipeline_.start("127.0.0.1", 5004, key, salt);

    transition_to(State::STREAMING);
  } else {
    std::cout << "[OtterSec] Handshake Failed. Tearing down.\n";
    transition_to(State::TEARDOWN);
  }
}

bool SessionManager::push_frame(const OtterFrameBuffer *frame) {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (current_state_ != State::STREAMING)
    return false;

  return pipeline_.push_frame(frame);
}

void SessionManager::shutdown() {
  std::lock_guard<std::mutex> lock(state_mutex_);
  if (current_state_ == State::IDLE)
    return;

  transition_to(State::TEARDOWN);
  if (server_)
    server_->stop();
  sec_context_.clear();

  pipeline_.stop();

  transition_to(State::IDLE);
}

void SessionManager::transition_to(State new_state) {
  current_state_ = new_state;
}

} // namespace ottersec
