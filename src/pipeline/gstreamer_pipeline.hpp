#ifndef OTTERSEC_GSTREAMER_PIPELINE_HPP
#define OTTERSEC_GSTREAMER_PIPELINE_HPP

#include "ottersec/types.h"
#include <gst/app/gstappsrc.h>
#include <gst/gst.h>
#include <string>
#include <vector>

namespace ottersec {
class GstreamerPipeline {
public:
  GstreamerPipeline();
  ~GstreamerPipeline();

  bool start(const std::string &dest_ip, uint16_t dest_port,
             const std::vector<uint8_t> &srtp_key,
             const std::vector<uint8_t> &srtp_salt);

  bool push_frame(const OtterFrameBuffer *frame);
  void stop();

private:
  GstElement *pipeline_{nullptr};
  GstElement *appsrc_{nullptr};
  bool is_playing_{false};
};
} // namespace ottersec

#endif
