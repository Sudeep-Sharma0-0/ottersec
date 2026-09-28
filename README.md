<img src="assets/ottersec.png" alt="OtterSec Logo" width="250">

# 🦦 OtterSec

OtterSec is a C++ low-latency, secure video streaming and telemetry middleware designed for edge computing and embedded hardware (e.g., Raspberry Pi 5 with Hailo AI accelerators). It bridges edge AI inference pipelines with external monitoring systems by providing out-of-band TLS key exchange and AES-128-ICM encrypted SRTP video streaming over UDP.

---

## Architecture Overview

OtterSec uses a two-phase architecture to isolate silent telemetry monitoring from high-bandwidth video transmission:


1. **Silent Tripwire Mode (TCP 8080):** The edge device runs a persistent TCP control server. It streams serialized Protocol Buffer (`pose_data.proto`) telemetry containing bounding boxes, skeletal keypoints, and inference flags. The backend stays connected silently without requesting video feed until an emergency flag (`fall_detected`) is asserted.
2. **Out-of-Band TLS Handshake (TCP 8443):** Upon an emergency trigger, OtterSec opens an OpenSSL TLS 1.3 server on port 8443. The backend client connects over TLS, and OtterSec generates 30 cryptographically secure pseudo-random bytes (16-byte AES Master Key + 14-byte Master Salt) via `RAND_bytes()`, transmitting them over the TLS tunnel.
3. **Encrypted SRTP Streaming (UDP 5004):** The TLS socket closes immediately after key transmission. OtterSec activates a hardware-accelerated GStreamer pipeline (`appsrc ! x264enc ! srtpenc ! udpsink`) to stream H.264 video encrypted with SRTP (`aes-128-icm`). Packets are tagged with a fixed Synchronization Source (`ssrc=112233`).
4. **Dynamic Decryption:** The backend receiver uses the 30-byte key received over TLS to configure an `srtpdec` pipeline in real time, decrypting the UDP stream on the fly.

---

## Key Features

- **Zero-Trust Key Distribution:** No static cryptographic keys stored on disk or hardcoded in binaries. Keys are generated dynamically per session using OpenSSL PRNG.
- **Memory Scrubbing:** Cryptographic buffers in C++ use `OPENSSL_cleanse()` upon session tear-down to erase key material from RAM.
- **Hardware-Optimized Video Pipeline:** Native integration with GStreamer `appsrc`, configured with zerolatency H.264 encoding options (`key-int-max=30`, `do-timestamp=true`).
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

## Prerequisites and Installation

### Dependencies

#### Edge Device (Linux / Raspberry Pi OS / Fedora)
- C++17 Compiler (GCC or Clang)
- CMake (>= 3.10)
- OpenSSL Development Files (`libssl-dev` or `openssl-devel`)
- GStreamer 1.0 Library & Core Plugins (`libgstreamer1.0-dev`, `gstreamer1-plugins-base-devel`, `gstreamer1-plugins-bad-free-devel`)
- Protocol Buffers Compiler & C++ Runtime (`protobuf-compiler`, `libprotobuf-dev`)
- pybind11 (`pybind11-dev` / `python3-pybind11`)

#### Backend Client (Linux / Fedora / macOS)
- GStreamer 1.0 CLI Tools & H.264 plugins (`gstreamer1-plugins-ugly`, `gstreamer1-libav`)
- Python 3.8+ or Node.js 18+

### Certificate Generation
Generate self-signed development certificates for TLS negotiation:

```bash
mkdir -p certs
openssl req -x509 -newkey rsa:4096 \
  -keyout certs/server.key \
  -out certs/server.crt \
  -days 365 -nodes \
  -subj "/CN=192.168.1.111"

```

### Build Instructions

```bash
# Clone and enter directory
cd ottersec

# Build C++ Shared Library and Python Extensions
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Install shared libraries globally (Optional)
sudo make install

```

---

## Usage Guide

### 1. Edge Device Integration (C++)

Integrate `libottersec` directly into your inference pipeline loop.

```cpp
#include <ottersec/api.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include <iostream>

int main() {
    // 1. Configure TLS Handshake parameters
    OtterHandshakeConfig config = {0};
    config.handshake_port = 8443;
    config.auth_timeout_ms = 5000;
    config.cert_path = "certs/server.crt";
    config.key_path = "certs/server.key";

    // 2. Initialize OtterSec Session and Control Server
    if (ottersec_init(&config) != 0) {
        std::cerr << "Failed to initialize OtterSec session.\n";
        return -1;
    }

    if (ottersec_start_control_server(8080) != 0) {
        std::cerr << "Failed to start Control Server on port 8080.\n";
        return -1;
    }

    // 3. Processing Loop
    cv::Mat frame = cv::Mat::zeros(640, 640, CV_8UC3); // Example RGB frame
    bool running = true;

    while (running) {
        // Push raw video frame to GStreamer pipeline
        OtterFrameBuffer fb = {0};
        fb.width = frame.cols;
        fb.height = frame.rows;
        fb.surface_ptr = frame.data;
        fb.pts = 0; // Hardware timestamp
        
        ottersec_push_frame(&fb);

        // Fall Detection Logic Event
        bool fall_detected = true; // Replace with actual fall detection code
        if (fall_detected) {
            // Send Protobuf Telemetry over TCP 8080
            OtterInferenceResult telemetry = {0};
            telemetry.timestamp_ms = 1000200;
            telemetry.fall_detected = 1;
            ottersec_send_telemetry(&telemetry);

            // Trigger Emergency Handshake Sequence
            OtterFallEvent fall_evt = {0};
            fall_evt.fall_detected = 1;
            fall_evt.confidence = 0.95f;
            ottersec_notify_fall(&fall_evt);
            
            break;
        }
    }

    // 4. Cleanup
    ottersec_stop_control_server();
    ottersec_terminate();
    return 0;
}

```

---

### 2. Backend Server Integration (Python)

The Python backend connects to TCP 8080, parses incoming Protobuf telemetry, and upon fall detection, executes a TLS handshake on TCP 8443 to pull the SRTP key and launch GStreamer.

```python
import socket
import struct
import binascii
import ssl
import subprocess
import time
import pose_data_pb2

EDGE_IP = '192.168.1.111'
CONTROL_PORT = 8080
TLS_PORT = 8443
VIDEO_PORT = 5004

def recvall(sock, count):
    buf = bytearray()
    while len(buf) < count:
        newbuf = sock.recv(count - len(buf))
        if not newbuf:
            return None
        buf.extend(newbuf)
    return buf

def wait_for_fall_alert():
    print(f"[Backend] Waiting for EdgeStreamer on {EDGE_IP}:{CONTROL_PORT}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    while True:
        try:
            sock.connect((EDGE_IP, CONTROL_PORT))
            break
        except ConnectionRefusedError:
            time.sleep(1)

    print("[Backend] Connected! Listening in SILENT TRIPWIRE mode...")

    try:
        while True:
            header = recvall(sock, 4)
            if not header:
                print("[Backend] Error: Connection closed by EdgeStreamer.")
                break

            msg_len = struct.unpack('>I', header)[0]
            payload = recvall(sock, msg_len)
            if not payload:
                break

            msg = pose_data_pb2.InferenceResult()
            msg.ParseFromString(payload)

            print(f"[Backend] Telemetry Parsed -> Fall Detected: {msg.fall_detected}, Persons: {len(msg.detections)}")

            if msg.fall_detected:
                print("\n[Backend] 🚨 CRITICAL ALERT: FALL DETECTED 🚨")
                sock.close()
                return True

    except Exception as e:
        print(f"\n[Backend] Stream Error: {e}")

    sock.close()
    return False

def fetch_tls_keys_and_play():
    print(f"\n[Backend] Connecting to TLS Port {TLS_PORT}...")

    context = ssl.create_default_context()
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE

    raw_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    try:
        time.sleep(0.2)
        secure_sock = context.wrap_socket(raw_socket, server_hostname=EDGE_IP)
        secure_sock.connect((EDGE_IP, TLS_PORT))
        print("[Backend] ✅ TLS Handshake Successful!")

        crypto_bytes = recvall(secure_sock, 30)
        if not crypto_bytes or len(crypto_bytes) != 30:
            print("[Backend] ❌ Failed to receive 30-byte encryption key!")
            return

        hex_key = binascii.hexlify(crypto_bytes).decode('utf-8')
        print(f"[Backend] 🔐 Key Received (Hex): {hex_key[:10]}...{hex_key[-10:]}")

        start_video_player(hex_key)

    except Exception as e:
        print(f"[Backend] ❌ TLS Handshake Failed: {e}")
    finally:
        secure_sock.close()

def start_video_player(hex_key):
    print(f"[Backend] 🎥 Launching ENCRYPTED Video Feed on UDP {VIDEO_PORT}...")

    srtp_caps = (
        f"application/x-srtp,payload=(int)96,ssrc=(uint)112233,srtp-cipher=(string)aes-128-icm,"
        f"srtp-auth=(string)hmac-sha1-80,srtcp-cipher=(string)aes-128-icm,"
        f"srtcp-auth=(string)hmac-sha1-80,srtp-key=(buffer){hex_key}"
    )

    gst_command = [
        "gst-launch-1.0", "--gst-debug-level=3", "-v",
        "udpsrc", f"port={VIDEO_PORT}",
        "!", srtp_caps,
        "!", "srtpdec",
        "!", "rtpjitterbuffer",
        "!", "rtph264depay",
        "!", "h264parse",
        "!", "avdec_h264",
        "!", "videoconvert",
        "!", "autovideosink", "sync=false"
    ]

    try:
        subprocess.run(gst_command)
    except FileNotFoundError:
        print("\n[ERROR] 'gst-launch-1.0' not found.")

if __name__ == "__main__":
    print("=== OtterSec Production Backend (Python) ===")
    if wait_for_fall_alert():
        fetch_tls_keys_and_play()
    print("\n[Backend] Session Ended.")

```

---

### 3. Backend Server Integration (Node.js)

Node.js asynchronous implementation using native `net`, `tls`, `child_process`, and `protobufjs`.

#### Prerequisites for Node.js

```bash
npm install protobufjs

```

#### Node.js Code (`backend.js`)

```javascript
const net = require('net');
const tls = require('tls');
const { spawn } = require('child_process');
const protobuf = require('protobufjs');

const EDGE_IP = '192.168.1.111';
const CONTROL_PORT = 8080;
const TLS_PORT = 8443;
const VIDEO_PORT = 5004;

async function startBackend() {
    console.log("=== OtterSec Production Backend (Node.js) ===");
    
    // Load Protobuf Definition
    const root = await protobuf.load('proto/pose_data.proto');
    const InferenceResult = root.lookupType('pose.InferenceResult');

    console.log(`[Backend JS] Waiting for EdgeStreamer on ${EDGE_IP}:${CONTROL_PORT}...`);

    const client = new net.Socket();

    const connectTripwire = () => {
        client.connect(CONTROL_PORT, EDGE_IP, () => {
            console.log('[Backend JS] Connected! Listening in SILENT TRIPWIRE mode...');
        });
    };

    connectTripwire();

    let buffer = Buffer.alloc(0);

    client.on('data', (chunk) => {
        buffer = Buffer.concat([buffer, chunk]);

        // Process 4-byte length-prefixed frames
        while (buffer.length >= 4) {
            const msgLen = buffer.readUInt32BE(0);
            if (buffer.length < 4 + msgLen) {
                break; // Wait for complete payload
            }

            const payload = buffer.subarray(4, 4 + msgLen);
            buffer = buffer.subarray(4 + msgLen);

            const msg = InferenceResult.decode(payload);
            console.log(`[Backend JS] Telemetry Parsed -> Fall Detected: ${msg.fallDetected}, Persons: ${msg.detections ? msg.detections.length : 0}`);

            if (msg.fallDetected) {
                console.log('\n[Backend JS] 🚨 CRITICAL ALERT: FALL DETECTED 🚨');
                client.destroy(); // Close tripwire connection
                fetchTlsKeysAndPlay();
                break;
            }
        }
    });

    client.on('error', (err) => {
        console.log(`[Backend JS] Connection error: ${err.message}. Retrying in 1s...`);
        setTimeout(connectTripwire, 1000);
    });
}

function fetchTlsKeysAndPlay() {
    console.log(`\n[Backend JS] Connecting to TLS Port ${TLS_PORT}...`);

    const options = {
        host: EDGE_IP,
        port: TLS_PORT,
        rejectUnauthorized: false // Development self-signed cert bypass
    };

    const secureSocket = tls.connect(options, () => {
        console.log('[Backend JS] ✅ TLS Handshake Successful!');
    });

    let cryptoBuffer = Buffer.alloc(0);

    secureSocket.on('data', (data) => {
        cryptoBuffer = Buffer.concat([cryptoBuffer, data]);

        if (cryptoBuffer.length >= 30) {
            const hexKey = cryptoBuffer.subarray(0, 30).toString('hex');
            console.log(`[Backend JS] 🔐 Key Received (Hex): ${hexKey.substring(0, 10)}...${hexKey.substring(50)}`);
            secureSocket.end();
            startVideoPlayer(hexKey);
        }
    });

    secureSocket.on('error', (err) => {
        console.error(`[Backend JS] ❌ TLS Error: ${err.message}`);
    });
}

function startVideoPlayer(hexKey) {
    console.log(`[Backend JS] 🎥 Launching ENCRYPTED Video Feed on UDP ${VIDEO_PORT}...`);

    const srtpCaps = `application/x-srtp,payload=(int)96,ssrc=(uint)112233,srtp-cipher=(string)aes-128-icm,srtp-auth=(string)hmac-sha1-80,srtcp-cipher=(string)aes-128-icm,srtcp-auth=(string)hmac-sha1-80,srtp-key=(buffer)${hexKey}`;

    const gstArgs = [
        '--gst-debug-level=3', '-v',
        'udpsrc', `port=${VIDEO_PORT}`,
        '!', srtpCaps,
        '!', 'srtpdec',
        '!', 'rtpjitterbuffer',
        '!', 'rtph264depay',
        '!', 'h264parse',
        '!', 'avdec_h264',
        '!', 'videoconvert',
        '!', 'autovideosink', 'sync=false'
    ];

    const gstProcess = spawn('gst-launch-1.0', gstArgs, { stdio: 'inherit' });

    gstProcess.on('close', (code) => {
        console.log(`\n[Backend JS] GStreamer exited with code ${code}`);
        console.log('[Backend JS] Session Ended.');
    });
}

startBackend();

```

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
| `ottersec_stop_control_server()` | `void` | Stops the TCP tripwire server independently. |
| `ottersec_terminate()` | `void` | Stops pipelines, clears key materials via `OPENSSL_cleanse()`, and resets session to `IDLE`. |

---

## Troubleshooting & Common Operations

### Port Conflicts / TIME_WAIT Status

If the C++ process terminates forcefully (`SIGKILL`), the OS kernel may hold TCP 8080 or 8443 in a `TIME_WAIT` state. Clear stuck ports on the edge device:

```bash
sudo fuser -k 8080/tcp
sudo fuser -k 8443/tcp

```

### Packet Drop Issues

If video freezes or drops during network congestion:

1. Verify `rtpjitterbuffer` is present in the GStreamer command.
2. Ensure firewall rules permit UDP port 5004 on the backend client (`sudo firewall-cmd --add-port=5004/udp --permanent` on Fedora/RHEL).
