#ifndef OTTERSEC_API_H
#define OTTERSEC_API_H

#include "types.h"
#ifdef __cplusplus
extern "C" {
#endif

int ottersec_init(const OtterHandshakeConfig *config);
int ottersec_notify_fall(const OtterFallEvent *event);
int ottersec_push_frame(const OtterFrameBuffer *frame);
void ottersec_terminate(void);

#ifdef __cplusplus
}
#endif

#endif
