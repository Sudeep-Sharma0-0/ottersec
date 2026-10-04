/**
 * @file api.h
 * @brief C-API function declarations for the OtterSec middleware.
 *
 * Exposes thread-safe bindings for initializing the TLS session manager,
 * handling fall events, pushing video frames, and managing the control server.
 */

#ifndef OTTERSEC_API_H
#define OTTERSEC_API_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes the secure OtterSec session manager.
 *
 * @param config Pointer to the configuration struct defining ports and
 * certificates.
 * @return 0 on success, -1 if a session is already initialized.
 */
int ottersec_init(const OtterHandshakeConfig *config);

/**
 * @brief Triggers the secure TLS handshake sequence upon fall detection.
 *
 * @param event Pointer to the simple fall event metadata.
 * @return 0 on successful state transition, -1 on failure.
 */
int ottersec_notify_fall(const OtterFallEvent *event);

/**
 * @brief Pushes a raw video frame into the active GStreamer SRTP pipeline.
 *
 * @param frame Pointer to the raw frame buffer.
 * @return 0 on successful push, -1 if not actively streaming.
 */
int ottersec_push_frame(const OtterFrameBuffer *frame);

/**
 * @brief Safely terminates all active network connections and pipelines.
 */
void ottersec_terminate(void);

/**
 * @brief Starts the silent tripwire TCP server for client discovery and
 * telemetry.
 *
 * @param port The TCP port to listen on (default 8080).
 * @return 0 on successful bind and listen, -1 on failure.
 */
int ottersec_start_control_server(int port);

/**
 * @brief Serializes inference data to Protobuf and dispatches it over TCP.
 *
 * @param result Pointer to the detailed inference telemetry struct.
 * @return 0 on successful transmission, -1 if no client is connected.
 */
int ottersec_send_telemetry(const OtterInferenceResult *result);

/**
 * @brief Stops the control server independently of the main session.
 */
void ottersec_stop_control_server(void);

/**
 * @brief Check the remote client to restart the tripwire.
 */
int ottersec_check_remote_resolve(void);

#ifdef __cplusplus
}
#endif

#endif // OTTERSEC_API_H
