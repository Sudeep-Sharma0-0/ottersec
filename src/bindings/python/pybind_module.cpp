#include "ottersec/api.h"
#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(pyottersec, m) {
  m.doc() = "OtterSec Python Inference Bindings";

  py::class_<OtterHandshakeConfig>(m, "HandshakeConfig")
      .def(py::init<>())
      .def_readwrite("handshake_port", &OtterHandshakeConfig::handshake_port);

  py::class_<OtterFallEvent>(m, "FallEvent")
      .def(py::init<>())
      .def_readwrite("fall_detected", &OtterFallEvent::fall_detected)
      .def_readwrite("confidence", &OtterFallEvent::confidence)
      .def_readwrite("bbox_x", &OtterFallEvent::bbox_x)
      .def_readwrite("bbox_y", &OtterFallEvent::bbox_y)
      .def_readwrite("bbox_w", &OtterFallEvent::bbox_w)
      .def_readwrite("bbox_h", &OtterFallEvent::bbox_h);

  py::class_<OtterFrameBuffer>(m, "FrameBuffer")
      .def(py::init<>())
      .def_readwrite("width", &OtterFrameBuffer::width)
      .def_readwrite("height", &OtterFrameBuffer::height)
      .def_readwrite("pts", &OtterFrameBuffer::pts)
      .def_readwrite("frame_num", &OtterFrameBuffer::frame_num);

  m.def(
      "init",
      [](const OtterHandshakeConfig &config) {
        return ottersec_init(&config) == 0;
      },
      "Initialize OtterSec");

  m.def(
      "notify_fall",
      [](const OtterFallEvent &event) {
        return ottersec_notify_fall(&event) == 0;
      },
      "Trigger Fall Protocol");

  m.def(
      "push_frame",
      [](const OtterFrameBuffer &frame) {
        return ottersec_push_frame(&frame) == 0;
      },
      "Push frame to active pipeline");

  m.def("terminate", []() { ottersec_terminate(); }, "Shutdown OtterSec");
}
