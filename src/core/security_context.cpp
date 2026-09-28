#include "security_context.hpp"
#include <openssl/crypto.h>
namespace ottersec {
SecurityContext::SecurityContext() = default;
SecurityContext::~SecurityContext() { clear(); }
SecurityContext::SecurityContext(SecurityContext &&other) noexcept
    : master_key_(std::move(other.master_key_)),
      master_salt_(std::move(other.master_salt_)),
      client_ip_(std::move(other.client_ip_)),
      srtp_port_(std::move(other.srtp_port_)),
      keys_set_(std::move(other.keys_set_)) {
  other.keys_set_ = false;
  other.srtp_port_ = 0;
}

SecurityContext &SecurityContext::operator=(SecurityContext &&other) noexcept {
  if (this != &other) {
    clear();

    master_key_ = std::move(other.master_key_);
    master_salt_ = std::move(other.master_salt_);
    client_ip_ = std::move(other.client_ip_);
    srtp_port_ = other.srtp_port_;
    keys_set_ = other.keys_set_;

    other.keys_set_ = false;
    other.srtp_port_ = 0;
  }
  return *this;
}

bool SecurityContext::set_keys(const uint8_t *master_key, size_t key_len,
                               const uint8_t *master_salt, size_t salt_len) {
  clear();

  if (!master_key || key_len != 16 || !master_salt || salt_len != 14) {
    return false;
  }

  master_key_.assign(master_key, master_key + key_len);
  master_salt_.assign(master_salt, master_salt + salt_len);
  keys_set_ = true;
  return true;
}

void SecurityContext::set_destination(const std::string &ip, uint16_t port) {
  client_ip_ = ip;
  srtp_port_ = port;
}

void SecurityContext::clear() {
  if (!master_key_.empty()) {
    OPENSSL_cleanse(master_key_.data(), master_key_.size());
    master_key_.clear();
  }
  if (!master_salt_.empty()) {
    OPENSSL_cleanse(master_salt_.data(), master_salt_.size());
    master_salt_.clear();
  }
  client_ip_.clear();
  srtp_port_ = 0;
  keys_set_ = false;
}
} // namespace ottersec
