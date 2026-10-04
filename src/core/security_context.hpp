#ifndef OTTER_SEC_SECURITY_CONTEXT_HPP
#define OTTER_SEC_SECURITY_CONTEXT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ottersec {
class SecurityContext {
public:
  SecurityContext();
  ~SecurityContext();

  SecurityContext(const SecurityContext &) = delete;
  SecurityContext &operator=(const SecurityContext) = delete;

  SecurityContext(SecurityContext &&other) noexcept;
  SecurityContext &operator=(SecurityContext &&other) noexcept;

  bool set_keys(const uint8_t *master_key, size_t key_len,
                const uint8_t *master_salt, size_t salt_len);
  void set_destination(const std::string &ip, uint16_t port);

  const std::vector<uint8_t> &get_master_key() const { return master_key_; }
  const std::vector<uint8_t> &get_master_salt() const { return master_salt_; }
  const std::string &get_client_ip() const { return client_ip_; }
  const uint16_t get_srtp_port() const { return srtp_port_; }

  void clear();
  bool is_valid() const { return keys_set_; }

private:
  std::vector<uint8_t> master_key_;
  std::vector<uint8_t> master_salt_;
  std::string client_ip_;
  uint16_t srtp_port_{0};
  bool keys_set_{false};
};
} // namespace ottersec

#endif
