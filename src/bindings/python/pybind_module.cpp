/**
 * @file pybindings.cpp
 * @brief Python bindings for the OtterSec Edge-to-Server middleware.
 *
 * Utilizes pybind11 to expose the internal C-API to Python environments.
 * This allows Python test scripts to easily mock edge events, trigger
 * the TLS handshake, and push simulated video frames into the pipeline
 * without needing the full Hailo C++ inference engine running.
 */

#include "ottersec/api.h"
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

namespace py = pybind11;

PYBIND11_MODULE(pyottersec, m) {
  m.doc() = "OtterSec Python Bindings: Secure Edge-to-Server Video Middleware";

  /**
   * @brief Binding for the OtterHandshakeConfig struct.
   */
  py::class_<OtterHandshakeConfig>(
      m, "HandshakeConfig", "Configuration for the TLS handshake server.")
      .def(py::init<>())
      .def_readwrite("handshake_port", &OtterHandshakeConfig::handshake_port,
                     "TCP port for the TLS server (default 8443).");

  /**
   * @brief Binding for the OtterFallEvent struct.
   */
  py::class_<OtterFallEvent>(
      m, "FallEvent",
      "Metadata describing a detected fall event from the Hailo NPU.")
      .def(py::init<>())
      .def_readwrite("fall_detected", &OtterFallEvent::fall_detected,
                     "Boolean flag indicating if a fall occurred.")
      .def_readwrite("confidence", &OtterFallEvent::confidence,
                     "Confidence score of the detection (0.0 to 1.0).")
      .def_readwrite("bbox_x", &OtterFallEvent::bbox_x,
                     "Bounding box X coordinate.")
      .def_readwrite("bbox_y", &OtterFallEvent::bbox_y,
                     "Bounding box Y coordinate.")
      .def_readwrite("bbox_w", &OtterFallEvent::bbox_w, "Bounding box width.")
      .def_readwrite("bbox_h", &OtterFallEvent::bbox_h, "Bounding box height.");

  /**
   * @brief Binding for the OtterFrameBuffer struct.
   */
  py::class_<OtterFrameBuffer>(
      m, "FrameBuffer", "Raw RGB video frame buffer for GStreamer ingestion.")
      .def(py::init<>())
      .def_readwrite("width", &OtterFrameBuffer::width,
                     "Image width in pixels.")
      .def_readwrite("height", &OtterFrameBuffer::height,
                     "Image height in pixels.")
      .def_readwrite("pts", &OtterFrameBuffer::pts,
                     "Hardware presentation timestamp.")
      .def_readwrite("frame_num", &OtterFrameBuffer::frame_num,
                     "Sequential frame identifier.");

  /**
   * @brief Core API Functions exposed to Python.
   */
  m.def(
      "init",
      [](const OtterHandshakeConfig &config) {
        return ottersec_init(&config) == 0;
      },
      py::arg("config"),
      "Initializes the OtterSec SessionManager state machine.");

  m.def(
      "notify_fall",
      [](const OtterFallEvent &event) {
        return ottersec_notify_fall(&event) == 0;
      },
      py::arg("event"),
      "Triggers the Fall Protocol, initiating the TLS handshake and encrypted "
      "video pipeline.");

  m.def(
      "push_frame",
      [](const OtterFrameBuffer &frame) {
        return ottersec_push_frame(&frame) == 0;
      },
      py::arg("frame"),
      "Pushes a raw FrameBuffer into the active SRTP GStreamer pipeline. "
      "Ignored if not in STREAMING state.");

  m.def(
      "start_control_server",
      [](int port) { return ottersec_start_control_server(port) == 0; },
      py::arg("port") = 8080,
      "Starts the silent tripwire TCP server for client discovery and "
      "telemetry.");

  m.def(
      "stop_control_server", []() { ottersec_stop_control_server(); },
      "Stops the silent tripwire TCP server.");

  m.def(
      "terminate", []() { ottersec_terminate(); },
      "Forcefully shuts down all TLS servers, pipelines, and clears "
      "cryptographic material.");
}
