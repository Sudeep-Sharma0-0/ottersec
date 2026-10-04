/**
 * @file security_context.cpp
 * @brief Manages the safe storage and lifecycle of cryptographic materials.
 *
 * The SecurityContext class acts as a secure memory vault for the SRTP
 * encryption keys generated during the TLS handshake. It ensures that
 * sensitive AES-128 keys and salts are securely wiped from RAM using
 * OpenSSL's memory cleanse routines before the object is destroyed.
 */

#include "security_context.hpp"
#include <openssl/crypto.h>
namespace ottersec {
/**
 * @brief Default constructor for SecurityContext.
 *
 * Initializes an empty, unkeyed security context.
 */
SecurityContext::SecurityContext() = default;

/**
 * @brief Destructor for SecurityContext.
 *
 * Automatically calls clear() to securely wipe the cryptographic
 * material from memory when the context goes out of scope.
 */
SecurityContext::~SecurityContext() { clear(); }

/**
 * @brief Move constructor for SecurityContext.
 *
 * Safely transfers ownership of cryptographic materials and connection
 * details from an existing context to a new one, leaving the original
 * context in a cleared, unkeyed state.
 *
 * @param other The rvalue reference to the context being moved.
 */
SecurityContext::SecurityContext(SecurityContext &&other) noexcept
    : master_key_(std::move(other.master_key_)),
      master_salt_(std::move(other.master_salt_)),
      client_ip_(std::move(other.client_ip_)),
      srtp_port_(std::move(other.srtp_port_)),
      keys_set_(std::move(other.keys_set_)) {
  other.keys_set_ = false;
  other.srtp_port_ = 0;
}

/**
 * @brief Move assignment operator for SecurityContext.
 *
 * Safely wipes any existing cryptographic material in the current object,
 * then transfers ownership from the provided context.
 *
 * @param other The rvalue reference to the context being moved.
 * @return SecurityContext& A reference to the newly assigned context.
 */
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

/**
 * @brief Securely stores the cryptographic keys required for SRTP encryption.
 *
 * Validates the byte lengths against AES-128 requirements. If valid, copies
 * the data into internal memory and marks the context as keyed. If invalid,
 * the context is cleared and remains unkeyed.
 *
 * @param master_key Pointer to the 16-byte SRTP AES key.
 * @param key_len Length of the master key (must be exactly 16).
 * @param master_salt Pointer to the 14-byte SRTP salt.
 * @param salt_len Length of the master salt (must be exactly 14).
 * @return true if the keys were successfully validated and stored.
 * @return false if the pointers are null or the lengths are incorrect.
 */
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

/**
 * @brief Sets the target destination for the encrypted video stream.
 *
 * @param ip The target IPv4 address as a string.
 * @param port The target UDP port.
 */
void SecurityContext::set_destination(const std::string &ip, uint16_t port) {
  client_ip_ = ip;
  srtp_port_ = port;
}

/**
 * @brief securely wipes all cryptographic material from RAM.
 *
 * Uses OPENSSL_cleanse to ensure that the compiler does not optimize away
 * the memory wipe, guaranteeing that the AES keys and salts cannot be
 * recovered from a core dump or memory inspection.
 */
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
