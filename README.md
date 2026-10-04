<img src="assets/ottersec.png" alt="OtterSec Logo" width="250">

# 🦦 OtterSec

[한국어 README (Korean)](./README_KR.md)

OtterSec is a high-performance, low-latency, secure video streaming and telemetry middleware written in C++ for edge computing devices and embedded accelerators (e.g., Raspberry Pi 5 with Hailo AI NPU modules). It bridges edge AI inference pipelines with remote monitoring systems by providing out-of-band TLS key distribution and real-time AES-128-ICM encrypted SRTP video streaming over UDP.

---

## Architecture Overview

OtterSec uses a two-phase state-machine architecture to isolate silent telemetry monitoring from high-bandwidth video transmission:

1. **Silent Tripwire Mode (TLS/TCP Port 8080):** The edge device runs a background control server. It streams serialized Protocol Buffer (`pose_data.proto`) telemetry containing bounding boxes, skeletal keypoints, and inference flags. The backend stays connected silently without requesting a video feed until an emergency flag (`fall_detected`) is asserted.
2. **Out-of-Band TLS Handshake (TCP Port 8443):** Upon an emergency trigger, OtterSec opens an OpenSSL TLS server on port 8443. The backend client connects over TLS, and OtterSec generates 30 cryptographically secure pseudo-random bytes (16-byte AES Master Key + 14-byte Master Salt) via `RAND_bytes()`, transmitting them across the TLS tunnel.
3. **Encrypted SRTP Streaming (UDP Port 5004):** The TLS socket closes immediately after key transmission. OtterSec activates a hardware-accelerated GStreamer pipeline (`appsrc ! videoconvert ! x264enc ! srtpenc ! udpsink`) to stream H.264 video encrypted with SRTP (`aes-128-icm`). Packets are constrained to `baseline` profile, zero B-frames (`bframes=0`), and tagged with a fixed Synchronization Source (`ssrc=112233`).
4. **Emergency Beacon & Remote Re-Arming:** During an active incident, OtterSec pulses emergency telemetry to ensure late-joining or restarted backend instances instantly pick up the alert. An operator can push an `OTTER_RESOLVE_CMD` over TLS to tear down the video pipeline, clear key materials from RAM, and re-arm the tripwire.

---

## Key Features

- **Zero-Trust Key Distribution:** No static cryptographic keys are stored on disk or hardcoded in binaries. Keys are generated dynamically per incident using OpenSSL PRNG.
- **Memory Scrubbing:** Cryptographic buffers in C++ use `OPENSSL_cleanse()` upon session teardown to erase key material from RAM.
- **Hardware-Optimized Video Pipeline:** Native integration with GStreamer `appsrc`, configured with web-compliant, zero-latency H.264 parameters (`key-int-max=30`, `bframes=0`, `profile=baseline`, `mtu=1400`, `bitrate=2048`).
- **Resilient Transport:** Built-in `rtpjitterbuffer` integration and explicit `SSRC` binding to prevent packet ordering issues and clock drift synchronization drops over wireless links.
- **Multi-Language Extensions:** Pure C-API boundary (`extern "C"`), Python bindings via `pybind11`, and Node.js interoperability.

---

## Repository Structure

```text
ottersec/
├── CMakeLists.txt
├── include/
│   └── ottersec/
│       ├── api.h
│       └── types.h
├── proto/
│   └── pose_data.proto
└── src/
    ├── api.cpp
    ├── bindings/
    │   └── python/
    │       ├── CMakeLists.txt
    │       └── pybind_module.cpp
    ├── core/
    │   ├── control_server.cpp
    │   ├── control_server.hpp
    │   ├── handshake_server.cpp
    │   ├── handshake_server.hpp
    │   ├── security_context.cpp
    │   ├── security_context.hpp
    │   ├── session_manager.cpp
    │   └── session_manager.hpp
    └── pipeline/
        ├── gstreamer_pipeline.cpp
        └── gstreamer_pipeline.hpp

```

---

## Environment Setup & Installation

### 1. Edge Device Setup (C++ / Linux / Fedora / Raspberry Pi OS)

#### Install System Dependencies

Install compiler toolchains, OpenSSL, GStreamer development libraries, and Protobuf compilers.

**On Fedora / RHEL:**

```bash
sudo dnf groupinstall -y "Development Tools" "C Development Tools and Libraries"
sudo dnf install -y cmake openssl-devel \
    gstreamer1-devel gstreamer1-plugins-base-devel gstreamer1-plugins-bad-free-devel \
    protobuf-compiler protobuf-devel pybind11-devel

```

**On Debian / Ubuntu / Raspberry Pi OS:**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libssl-dev \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
    protobuf-compiler libprotobuf-dev pybind11-dev

```

#### Generate Development TLS Certificates

Generate self-signed development certificates for out-of-band TLS negotiation:

```bash
mkdir -p certs
openssl req -x509 -newkey rsa:4096 \
  -keyout certs/server.key \
  -out certs/server.crt \
  -days 365 -nodes \
  -subj "/CN=127.0.0.1"

```

#### Compile and Install `libottersec`

```bash
# Clone repository
git clone https://github.com/Sudeep-Sharma0-0/ottersec.git
cd ottersec

# Build C++ Shared Library and Python Extension
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install shared library and headers system-wide
sudo make install
sudo ldconfig

```

#### Example Code Structure
```cpp
#include <ottersec/api.h>
#include <iostream>

int main() {
    // 1. Configure and Initialize OtterSec
    OtterHandshakeConfig config = {8443, 5000, "certs/server.crt", "certs/server.key"};
    ottersec_init(&config);
    ottersec_start_control_server(8080); // Start Silent Tripwire

    std::cout << "[Edge] System Armed. Monitoring...\n";

    while (true) {
        // 2. Push raw frames to the GStreamer pipeline buffer
        OtterFrameBuffer fb = {640, 640, my_rgb_frame_ptr, 0};
        ottersec_push_frame(&fb);

        // 3. Trigger Emergency Secure Stream on Detection
        if (check_if_fall_detected()) {
            std::cout << "[Edge] Fall Detected! Initiating Secure Video Stream...\n";
            OtterFallEvent evt = {1, 0.95f}; // confidence = 0.95
            ottersec_notify_fall(&evt); 
        }

        // 4. Remote Re-arm via Dashboard
        if (ottersec_check_remote_resolve()) {
            std::cout << "[Edge] Incident Resolved remotely. Re-arming...\n";
            ottersec_terminate();
            ottersec_init(&config);
            ottersec_start_control_server(8080);
        }
    }
    return 0;
}
```

---

### 2. Python Backend Setup

The Python backend connects to TCP 8080, parses incoming Protobuf telemetry, executes a TLS handshake on TCP 8443 upon fall detection, and launches GStreamer to play the decrypted SRTP stream.

#### Install Python Dependencies & Generate Protobuf Stubs

```bash
# Create a virtual environment (optional)
python3 -m venv venv
source venv/bin/activate

# Install requirements
pip install protobuf

# Compile the Protocol Buffer definition for Python
protoc -I=proto --python_out=. proto/pose_data.proto

```

#### Example Python Backend Code
```python
import socket
import ssl
import subprocess

EDGE_IP = '192.168.1.111'

# 1. Connect to Silent Tripwire
print("[Backend] Connecting to Tripwire...")
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((EDGE_IP, 8080))

# Wait for telemetry packet indicating a fall
data = sock.recv(1024) 
print("[Backend] 🚨 ALERT RECEIVED! Fetching keys...")
sock.close()

# 2. Connect via TLS to fetch AES Key
context = ssl.create_default_context()
context.check_hostname = False
context.verify_mode = ssl.CERT_NONE

with socket.create_connection((EDGE_IP, 8443)) as raw_sock:
    with context.wrap_socket(raw_sock, server_hostname=EDGE_IP) as secure_sock:
        crypto_bytes = secure_sock.recv(30)
        hex_key = crypto_bytes.hex()
        print(f"[Backend] 🔐 Key Received: {hex_key}")

# 3. Launch GStreamer to decrypt video
srtp_caps = f"application/x-srtp,payload=(int)96,ssrc=(uint)112233,srtp-cipher=(string)aes-128-icm,srtp-auth=(string)hmac-sha1-80,srtcp-cipher=(string)aes-128-icm,srtcp-auth=(string)hmac-sha1-80,srtp-key=(buffer){hex_key}"

subprocess.run([
    "gst-launch-1.0", "udpsrc", "port=5004",
    "!", srtp_caps, "!", "srtpdec",
    "!", "rtph264depay", "!", "h264parse", "!", "avdec_h264", "!", "autovideosink"
])
```

#### Run the Python Backend

```bash
python3 backend.py

```

---

### 3. Node.js Backend & Dashboard Setup

The Node.js backend handles TLS handshakes, Protobuf telemetry decoding, headless SRTP video decryption via GStreamer, and real-time WebSocket broadcasting to an HTML5 dashboard (`JMuxer`).

#### Install Node.js Dependencies

```bash
# Initialize Node project and install dependencies
npm install express ws protobufjs

```

#### Directory Setup

Ensure your project contains the compiled `pose_data.proto` file and an HTML5 frontend:

```text
project-root/
├── server.js
├── pose_data.proto
└── public/
    └── index.html

```

##### server.js
```js
const net = require('net');
const tls = require('tls');
const { spawn } = require('child_process');

const EDGE_IP = '192.168.1.111';

// 1. Connect to Silent Tripwire
const client = new net.Socket();
client.connect(8080, EDGE_IP, () => {
    console.log('[Backend] Connected to Tripwire. Waiting for events...');
});

client.on('data', (data) => {
    // (In production, decode Protobuf here)
    console.log('[Backend] 🚨 ALERT RECEIVED! Fetching secure video keys...');
    client.destroy();
    
    // 2. Connect via TLS to fetch AES SRTP Key
    const secureSocket = tls.connect({ host: EDGE_IP, port: 8443, rejectUnauthorized: false }, () => {
        console.log('[Backend] TLS Secured.');
    });

    secureSocket.on('data', (cryptoBytes) => {
        const hexKey = cryptoBytes.toString('hex');
        console.log(`[Backend] 🔐 Key Received: ${hexKey}`);
        secureSocket.end();
        
        // 3. Launch GStreamer to Decrypt UDP 5004 Video Stream
        const srtpCaps = `application/x-srtp,payload=(int)96,ssrc=(uint)112233,srtp-cipher=(string)aes-128-icm,srtp-auth=(string)hmac-sha1-80,srtcp-cipher=(string)aes-128-icm,srtcp-auth=(string)hmac-sha1-80,srtp-key=(buffer)${hexKey}`;
        
        spawn('gst-launch-1.0', [
            'udpsrc', 'port=5004', 
            '!', srtpCaps, 
            '!', 'srtpdec', 
            '!', 'rtph264depay', '!', 'h264parse', '!', 'avdec_h264', '!', 'autovideosink'
        ], { stdio: 'inherit' });
    });
});
```

#### Run the Node.js Server

```bash
node server.js

```

Open a browser and navigate to `http://localhost:3000`.

---

## API Reference (C-API)

All C-API functions are exposed via `#include <ottersec/api.h>` and contained within `extern "C"` blocks.

| Function | Return Type | Description |
| --- | --- | --- |
| `ottersec_init(const OtterHandshakeConfig *config)` | `int` | Initializes the session state machine and loads TLS certificates. Returns `0` on success. |
| `ottersec_start_control_server(int port)` | `int` | Starts the persistent TCP tripwire server (default port `8080`). Returns `0` on success. |
| `ottersec_send_telemetry(const OtterInferenceResult *result)` | `int` | Serializes telemetry to Protobuf and sends length-prefixed bytes over TCP. |
| `ottersec_notify_fall(const OtterFallEvent *event)` | `int` | Triggers state transition from `IDLE` to `HANDSHAKING`, spinning up the TLS server on port `8443`. |
| `ottersec_push_frame(const OtterFrameBuffer *frame)` | `int` | Ingests a raw RGB frame into `appsrc`. Active only during `STREAMING` state. |
| `ottersec_check_remote_resolve()` | `int` | Thread-safely checks if `OTTER_RESOLVE_CMD` was received and resets the atomic flag. |
| `ottersec_stop_control_server()` | `void` | Stops the TCP tripwire server independently. |
| `ottersec_terminate()` | `void` | Stops pipelines, clears key materials via `OPENSSL_cleanse()`, and resets session to `IDLE`. |

---

## Troubleshooting

### Stuck Ports / TIME_WAIT

If the process exits unexpectedly, the kernel may hold ports `8080` or `8443` in `TIME_WAIT`. Clear them manually:

```bash
sudo fuser -k 8080/tcp
sudo fuser -k 8443/tcp

```

### Video Smearing or Green Frames

Green artifacting indicates memory race conditions during zero-copy buffer passing. Ensure `push_frame()` allocates an isolated `GstBuffer` memory block via `gst_buffer_new_allocate()` and copies raw frame memory before handing it off to GStreamer.
