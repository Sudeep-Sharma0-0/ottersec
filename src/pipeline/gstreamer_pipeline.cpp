#include "gstreamer_pipeline.hpp"
#include <cstring>
#include <iostream>

namespace ottersec {

GstreamerPipeline::GstreamerPipeline() {
  if (!gst_is_initialized()) {
    gst_init(nullptr, nullptr);
  }
}

GstreamerPipeline::~GstreamerPipeline() { stop(); }

bool GstreamerPipeline::start(const std::string &dest_ip, uint16_t dest_port,
                              const std::vector<uint8_t> &srtp_key,
                              const std::vector<uint8_t> &srtp_salt) {
  if (is_playing_)
    return false;

  std::string pipeline_desc =
      "appsrc name=appsrc is-live=true format=time do-timestamp=true ! "
      "video/x-raw,format=RGB,width=640,height=640,framerate=30/1 ! "
      "videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast "
      "key-int-max=30 ! "
      "rtph264pay pt=96 ssrc=112233 config-interval=1 ! srtpenc "
      "name=srtpcrypto ! "
      "udpsink host=" +
      dest_ip + " port=" + std::to_string(dest_port);

  GError *error = nullptr;
  pipeline_ = gst_parse_launch(pipeline_desc.c_str(), &error);

  if (error) {
    std::cerr << "[OtterSec] GStreamer Error: " << error->message << "\n";
    g_error_free(error);
    return false;
  }

  appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "appsrc");
  if (!appsrc_) {
    std::cerr << "[OtterSec] FATAL: Could not find appsrc!\n";
    return false;
  }

  GstElement *srtpenc = gst_bin_get_by_name(GST_BIN(pipeline_), "srtpcrypto");
  if (srtpenc) {
    std::vector<uint8_t> crypto_material;
    crypto_material.insert(crypto_material.end(), srtp_key.begin(),
                           srtp_key.end());
    crypto_material.insert(crypto_material.end(), srtp_salt.begin(),
                           srtp_salt.end());

    GstBuffer *key_buffer = gst_buffer_new_allocate(nullptr, 30, nullptr);
    gst_buffer_fill(key_buffer, 0, crypto_material.data(), 30);

    g_object_set(srtpenc, "key", key_buffer, "rtp-cipher", 1, "rtcp-cipher", 1,
                 nullptr);

    gst_buffer_unref(key_buffer);
    gst_object_unref(srtpenc);
  }

  gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  is_playing_ = true;
  std::cout << "[OtterSec] GStreamer Pipeline ACTIVE and ENCRYPTED.\n";

  return true;
}

bool GstreamerPipeline::push_frame(const OtterFrameBuffer *frame) {
  if (!is_playing_ || !appsrc_ || !frame)
    return false;

  size_t buffer_size = frame->width * frame->height * 3;

  GstBuffer *gst_buf = gst_buffer_new_allocate(nullptr, buffer_size, nullptr);

  if (frame->surface_ptr) {
    gst_buffer_fill(gst_buf, 0, frame->surface_ptr, buffer_size);
  } else {
    gst_buffer_memset(gst_buf, 0, 0x00FF00, buffer_size);
  }

  GST_BUFFER_PTS(gst_buf) = frame->pts;

  GstFlowReturn ret;
  g_signal_emit_by_name(appsrc_, "push-buffer", gst_buf, &ret);
  gst_buffer_unref(gst_buf);

  return (ret == GST_FLOW_OK);
}

void GstreamerPipeline::stop() {
  if (pipeline_) {
    gst_element_send_event(pipeline_, gst_event_new_eos());
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    gst_object_unref(pipeline_);
    pipeline_ = nullptr;
  }
  if (appsrc_) {
    gst_object_unref(appsrc_);
    appsrc_ = nullptr;
  }
  is_playing_ = false;
}

} // namespace ottersec
