#ifndef OTTERSEC_TYPES_H
#define OTTERSEC_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t width;
  uint32_t height;
  void *surface_ptr;
  uint64_t pts;
  uint64_t frame_num;
} OtterFrameBuffer;

typedef struct {
  uint8_t fall_detected;
  float confidence;
  uint32_t bbox_x;
  uint32_t bbox_y;
  uint32_t bbox_w;
  uint32_t bbox_h;
  uint64_t timestamp_utc;
} OtterFallEvent;

typedef struct {
  uint16_t handshake_port;
  uint32_t auth_timeout_ms;
  const char *cert_path;
  const char *key_path;
} OtterHandshakeConfig;

#ifdef __cplusplus
}
#endif

#endif
