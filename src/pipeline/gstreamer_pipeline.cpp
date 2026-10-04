/**
 * @file gstreamer_pipeline.cpp
 * @brief Manages the secure video encoding and transmission pipeline.
 *
 * This class handles the initialization of a GStreamer pipeline that takes
 * raw RGB frames, encodes them via x264, applies SRTP (Secure Real-time
 * Transport Protocol) encryption using dynamically generated TLS keys,
 * and streams them over UDP to the connected Python backend.
 */
#include "gstreamer_pipeline.hpp"
#include <cstring>
#include <iostream>
#include <openssl/crypto.h>

namespace ottersec {
/**
 * @brief Constructs a new GstreamerPipeline object.
 *
 * Safely initializes the global GStreamer library if it has not been
 * initialized yet by the host application.
 */
GstreamerPipeline::GstreamerPipeline() {
  if (!gst_is_initialized()) {
    gst_init(nullptr, nullptr);
  }
}

/**
 * @brief Destroys the pipeline and ensures all GStreamer resources are freed.
 */
GstreamerPipeline::~GstreamerPipeline() { stop(); }

/**
 * @brief Starts the encrypted UDP video stream to the target client.
 *
 * Dynamically constructs an x264 encoding pipeline. It enforces a strict
 * 30-frame keyframe interval and forces timestamp generation to prevent
 * dropping frames. Finally, it injects the 30-byte cryptographic material into
 * the `srtpenc` plugin.
 *
 * @param dest_ip The dynamically discovered IP address of the listening client.
 * @param dest_port The UDP port to stream to (typically 5004).
 * @param srtp_key A 16-byte cryptographic key generated during the TLS
 * handshake.
 * @param srtp_salt A 14-byte cryptographic salt generated during the TLS
 * handshake.
 * @return true if the pipeline was successfully constructed and set to PLAYING.
 * @return false if the pipeline is already running, or if element creation
 * fails.
 */
bool GstreamerPipeline::start(const std::string &dest_ip, uint16_t dest_port,
                              const std::vector<uint8_t> &srtp_key,
                              const std::vector<uint8_t> &srtp_salt) {
  if (is_playing_)
    return false;

  // Build the secure, zero-latency H.264 UDP pipeline.
  // Note: ssrc=112233 is hardcoded to perfectly match the decrypter's
  // expectations.
  std::string pipeline_desc =
      "appsrc name=appsrc is-live=true format=time do-timestamp=true ! "
      "video/x-raw,format=RGB,width=640,height=640,framerate=30/1 ! "
      "videoconvert ! video/x-raw,format=I420 ! "
      "x264enc bitrate=2048 tune=zerolatency speed-preset=ultrafast "
      "key-int-max=30 bframes=0 ! "
      "video/x-h264,profile=baseline ! "
      "rtph264pay mtu=1400 pt=96 ssrc=112233 config-interval=1 ! srtpenc "
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

    OPENSSL_cleanse(crypto_material.data(), crypto_material.size());
  }

  gst_element_set_state(pipeline_, GST_STATE_PLAYING);
  is_playing_ = true;
  std::cout << "[OtterSec] GStreamer Pipeline ACTIVE and ENCRYPTED.\n";

  return true;
}

/**
 * @brief Ingests a raw RGB frame, wraps it in a GstBuffer, and pushes it to the
 * pipeline.
 *
 * @param frame A pointer to the OtterFrameBuffer struct containing the raw
 * image data and hardware presentation timestamp (PTS).
 * @return true if the buffer was successfully pushed into the appsrc element.
 * @return false if the pipeline is stopped, appsrc is missing, or push fails.
 */
bool GstreamerPipeline::push_frame(const OtterFrameBuffer *frame) {
  if (!is_playing_ || !appsrc_ || !frame || !frame->surface_ptr)
    return false;

  size_t buffer_size = frame->width * frame->height * 3;
  GstBuffer *gst_buf = gst_buffer_new_allocate(nullptr, buffer_size, nullptr);
  gst_buffer_fill(gst_buf, 0, frame->surface_ptr, buffer_size);

  GST_BUFFER_PTS(gst_buf) = GST_CLOCK_TIME_NONE;

  GstFlowReturn ret;
  g_signal_emit_by_name(appsrc_, "push-buffer", gst_buf, &ret);
  gst_buffer_unref(gst_buf);
  return (ret == GST_FLOW_OK);
}

/**
 * @brief Safely shuts down the pipeline, sends the End-Of-Stream (EOS) event,
 *        and unreferences all GStreamer objects to prevent memory leaks.
 */
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
