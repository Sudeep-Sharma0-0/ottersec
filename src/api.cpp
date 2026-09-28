/**
 * @file api.cpp
 * @brief C-API implementation for the OtterSec Edge-to-Server middleware.
 *
 * Provides thread-safe C bindings for the internal C++ SessionManager
 * and ControlServer. All state transitions, network operations, and
 * pipeline bindings are guarded by a global mutex.
 */
#include "ottersec/api.h"
#include "core/control_server.hpp"
#include "core/session_manager.hpp"
#include "pose_data.pb.h"
#include <memory>
#include <mutex>

/** Global active session manager for handling TLS and video streaming. */
static std::unique_ptr<ottersec::SessionManager> g_session = nullptr;

/** Global mutex ensuring thread-safe access across the C-API boundary. */
static std::mutex g_api_mutex;

namespace ottersec {
/** Global control server for handling the silent tripwire and dynamic IP
 * discovery. */
std::unique_ptr<ControlServer> g_control_server = nullptr;
} // namespace ottersec

extern "C" {

/**
 * @brief Initializes the OtterSec session manager.
 *
 * @param config Pointer to the configuration struct defining ports and
 * certificates.
 * @return 0 on success, -1 if a session is already initialized.
 */
int ottersec_init(const OtterHandshakeConfig *config) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (g_session)
    return -1;
  g_session = std::make_unique<ottersec::SessionManager>(config);
  return 0;
}

/**
 * @brief Triggers the secure TLS handshake sequence upon fall detection.
 *
 * @param event Pointer to the fall event metadata.
 * @return 0 on successful state transition, -1 if no session exists or
 * transition fails.
 */
int ottersec_notify_fall(const OtterFallEvent *event) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (!g_session)
    return -1;
  return g_session->trigger_fall_event(event) ? 0 : -1;
}

/**
 * @brief Pushes a raw video frame into the active GStreamer SRTP pipeline.
 *
 * @param frame Pointer to the raw frame buffer (e.g., RGB data from
 * Hailo/V4L2).
 * @return 0 on successful push, -1 if the pipeline is not active or session is
 * null.
 */
int ottersec_push_frame(const OtterFrameBuffer *frame) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (!g_session)
    return -1;
  return g_session->push_frame(frame) ? 0 : -1;
}

/**
 * @brief Safely terminates all active network connections, pipelines, and
 * servers.
 *
 * Frees the underlying memory for both the SessionManager and ControlServer.
 */
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

/**
 * @brief Starts the silent tripwire TCP server for client discovery and
 * telemetry.
 *
 * @param port The TCP port to listen on (default 8080).
 * @return 0 on successful bind and listen, -1 if already running or bind fails.
 */
int ottersec_start_control_server(int port) {
  std::lock_guard<std::mutex> lock(g_api_mutex);

  if (ottersec::g_control_server)
    return -1;

  ottersec::g_control_server = std::make_unique<ottersec::ControlServer>(port);
  return ottersec::g_control_server->start() ? 0 : -1;
}

/**
 * @brief Serializes inference data to Protobuf and dispatches it to the
 * connected client.
 *
 * @param result Pointer to the C-struct containing bounding boxes, keypoints,
 * and flags.
 * @return 0 on successful network transmission, -1 if serialization fails or no
 * client is connected.
 */
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

/**
 * @brief Stops the control server independently of the main session.
 */
void ottersec_stop_control_server(void) {
  std::lock_guard<std::mutex> lock(g_api_mutex);
  if (ottersec::g_control_server) {
    ottersec::g_control_server->stop();
    ottersec::g_control_server.reset();
  }
}
}
