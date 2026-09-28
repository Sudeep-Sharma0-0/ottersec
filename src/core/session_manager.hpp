#pragma once
#include "../pipeline/gstreamer_pipeline.hpp"
#include "handshake_server.hpp"
#include "ottersec/types.h"
#include "security_context.hpp"
#include <memory>
#include <mutex>

namespace ottersec {
class SessionManager {
public:
  enum class State { IDLE, HANDSHAKING, STREAMING, TEARDOWN };

  SessionManager(const OtterHandshakeConfig *config);
  ~SessionManager();

  bool trigger_fall_event(const OtterFallEvent *event);
  bool push_frame(const OtterFrameBuffer *frame);
  void shutdown();

private:
  void transition_to(State new_state);
  void on_handshake_complete(bool success, const std::vector<uint8_t> &key,
                             const std::vector<uint8_t> &salt);

  State current_state_{State::IDLE};
  std::mutex state_mutex_;
  uint16_t handshake_port_;
  std::string cert_path_;
  std::string key_path_;

  SecurityContext sec_context_;
  std::unique_ptr<HandshakeServer> server_;

  GstreamerPipeline pipeline_;
};
} // namespace ottersec
