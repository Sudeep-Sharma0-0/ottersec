#include "ottersec/api.h"
#include "core/control_server.hpp"
#include "core/session_manager.hpp"
#include "pose_data.pb.h"
#include <memory>
#include <mutex>

static std::unique_ptr<ottersec::SessionManager> g_session = nullptr;
static std::mutex g_api_mutex;

namespace ottersec {
std::unique_ptr<ControlServer> g_control_server = nullptr;
}

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

  if (ottersec::g_control_server) {
    ottersec::g_control_server->stop();
    ottersec::g_control_server.reset();
  }
}

int ottersec_start_control_server(int port) {
  std::lock_guard<std::mutex> lock(g_api_mutex);

  if (ottersec::g_control_server)
    return -1;

  ottersec::g_control_server = std::make_unique<ottersec::ControlServer>(port);
  return ottersec::g_control_server->start() ? 0 : -1;
}

int ottersec_send_telemetry(const OtterInferenceResult *result) {
  if (!result || !ottersec::g_control_server)
    return -1;

  std::cout << "[OtterSec] C-API received fall_detected: "
            << result->fall_detected << "\n";

  pose::InferenceResult msg;
  msg.set_timestamp_ms(result->timestamp_ms);
  msg.set_fall_detected(result->fall_detected > 0);

  for (size_t i = 0; i < result->num_detections; ++i) {
    const auto &det = result->detections[i];
    pose::Detection *det_msg = msg.add_detections();

    det_msg->set_track_id(det.track_id);
    det_msg->set_confidence(det.confidence);
    det_msg->set_x_min(det.x_min);
    det_msg->set_y_min(det.y_min);
    det_msg->set_width(det.width);
    det_msg->set_height(det.height);

    for (size_t j = 0; j < det.num_keypoints; ++j) {
      pose::Keypoint *kp_msg = det_msg->add_keypoints();
      kp_msg->set_x(det.keypoints[j].x);
      kp_msg->set_y(det.keypoints[j].y);
      kp_msg->set_confidence(det.keypoints[j].confidence);
    }
  }

  std::string payload;
  if (msg.SerializeToString(&payload)) {
    ottersec::g_control_server->send_raw_payload(
        reinterpret_cast<const uint8_t *>(payload.data()), payload.size());
    return 0;
  }

  return -1;
}

void ottersec_stop_control_server(void) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (ottersec::g_control_server) {
    ottersec::g_control_server->stop();
    ottersec::g_control_server.reset();
  }
}
}
