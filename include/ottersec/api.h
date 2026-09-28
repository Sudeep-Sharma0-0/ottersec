#ifndef OTTERSEC_API_H
#define OTTERSEC_API_H

#include "types.h"
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  float x;
  float y;
  float confidence;
} OtterKeypoint;

typedef struct {
  int32_t track_id;
  float confidence;
  float x_min;
  float y_min;
  float width;
  float height;
  const OtterKeypoint *keypoints;
  size_t num_keypoints;
} OtterDetection;

typedef struct {
  uint64_t timestamp_ms;
  uint32_t fall_detected;
  uint32_t _padding;
  const OtterDetection *detections;
  size_t num_detections;
} OtterInferenceResult;

int ottersec_init(const OtterHandshakeConfig *config);
int ottersec_notify_fall(const OtterFallEvent *event);
int ottersec_push_frame(const OtterFrameBuffer *frame);
void ottersec_terminate(void);

int ottersec_start_control_server(int port);
int ottersec_send_telemetry(const OtterInferenceResult *result);
void ottersec_stop_control_server(void);

#ifdef __cplusplus
}
#endif

#endif
