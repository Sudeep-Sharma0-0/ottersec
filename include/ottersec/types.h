/**
 * @file types.h
 * @brief Core data structures for the OtterSec C-API.
 *
 * This file defines all the structs used to pass data across the C/C++
 * boundary, including video frames, fall events, detailed telemetry,
 * and server configurations.
 */

#ifndef OTTERSEC_TYPES_H
#define OTTERSEC_TYPES_H

#include <stdbool.h>
#include <stddef.h> /* Used instead of <cstddef> for pure C compatibility */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Represents a single raw video frame to be encoded and streamed.
 */
typedef struct {
  uint32_t width;     /**< Image width in pixels. */
  uint32_t height;    /**< Image height in pixels. */
  void *surface_ptr;  /**< Pointer to the raw RGB pixel data in memory. */
  uint64_t pts;       /**< Hardware presentation timestamp. */
  uint64_t frame_num; /**< Sequential frame identifier. */
} OtterFrameBuffer;

/**
 * @brief Simplified metadata for triggering the fall protocol.
 */
typedef struct {
  uint8_t fall_detected;  /**< Non-zero if a fall is actively detected. */
  float confidence;       /**< Confidence score of the fall (0.0 to 1.0). */
  uint32_t bbox_x;        /**< Bounding box top-left X coordinate. */
  uint32_t bbox_y;        /**< Bounding box top-left Y coordinate. */
  uint32_t bbox_w;        /**< Bounding box width. */
  uint32_t bbox_h;        /**< Bounding box height. */
  uint64_t timestamp_utc; /**< UTC timestamp of the event. */
} OtterFallEvent;

/**
 * @brief Configuration parameters for the TLS Handshake Server.
 */
typedef struct {
  uint16_t handshake_port; /**< TCP port for the TLS server (default 8443). */
  uint32_t
      auth_timeout_ms;   /**< Maximum time allowed for client authentication. */
  const char *cert_path; /**< Path to the server's public certificate (PEM). */
  const char *key_path;  /**< Path to the server's private key (PEM). */
} OtterHandshakeConfig;

/**
 * @brief Represents a single skeletal keypoint (e.g., shoulder, knee).
 */
typedef struct {
  float x;          /**< Normalized X coordinate (0.0 to 1.0). */
  float y;          /**< Normalized Y coordinate (0.0 to 1.0). */
  float confidence; /**< Confidence score for this specific keypoint. */
} OtterKeypoint;

/**
 * @brief Represents a tracked person and their skeletal structure.
 */
typedef struct {
  int32_t track_id;               /**< Unique ID for the tracked individual. */
  float confidence;               /**< Overall detection confidence. */
  float x_min;                    /**< Bounding box left edge. */
  float y_min;                    /**< Bounding box top edge. */
  float width;                    /**< Bounding box width. */
  float height;                   /**< Bounding box height. */
  const OtterKeypoint *keypoints; /**< Array of skeletal keypoints. */
  size_t num_keypoints; /**< Number of elements in the keypoints array. */
} OtterDetection;

/**
 * @brief Detailed inference telemetry for the Tripwire Control Server.
 */
typedef struct {
  uint64_t timestamp_ms;  /**< Millisecond timestamp of inference. */
  uint32_t fall_detected; /**< Non-zero if a fall is detected in this frame. */
  uint32_t _padding;      /**< Struct padding for memory alignment. */
  const OtterDetection *detections; /**< Array of detected individuals. */
  size_t num_detections; /**< Number of elements in the detections array. */
} OtterInferenceResult;

#ifdef __cplusplus
}
#endif

#endif // OTTERSEC_TYPES_H
