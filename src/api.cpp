#include "ottersec/api.h"
#include "core/session_manager.hpp"
#include <memory>
#include <mutex>

static std::unique_ptr<ottersec::SessionManager> g_session = nullptr;
static std::mutex g_api_mutex;

extern "C" {

int ottersec_init(const OtterHandshakeConfig *config) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (g_session)
    return -1;
  g_session = std::make_unique<ottersec::SessionManager>(config);
  return 0;
}

int ottersec_notify_fall(const OtterFallEvent *event) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (!g_session)
    return -1;
  return g_session->trigger_fall_event(event) ? 0 : -1;
}

int ottersec_push_frame(const OtterFrameBuffer *frame) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (!g_session)
    return -1;
  return g_session->push_frame(frame) ? 0 : -1;
}

void ottersec_terminate(void) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (g_session) {
    g_session->shutdown();
    g_session.reset();
  }
}
}
