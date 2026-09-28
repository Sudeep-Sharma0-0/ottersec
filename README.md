# 🦦 OtterSec

**OtterSec** is a C++ based secure media streaming middleware designed for edge computing and AI devices. Built to integrate seamlessly with hardware-accelerated computer vision pipelines (such as Hailo NPUs on Raspberry Pi), OtterSec bridges the gap between raw edge inference and secure network transmission.

It implements a two-phase architecture: an out-of-band **TLS 1.3 Handshake** to securely negotiate cryptographic keys, followed by a zero-latency, hardware-accelerated **SRTP (Secure Real-time Transport Protocol)** video stream over UDP.

---

## How It Works

OtterSec solves the fundamental problem of streaming encrypted video from edge devices without hardcoding keys or using vulnerable static encryption parameters.

1. **Trigger & Listen:** The edge device monitors for a specific hardware or AI event (e.g., fall detection). Once triggered, OtterSec opens a secure TLS server on a specified port (default `8443`).
2. **TLS Handshake:** The backend client connects to the edge device. The edge device verifies the connection using standard OpenSSL certificates (`server.crt` and `server.key`).
3. **Key Exchange:** Upon a successful TLS handshake, OtterSec generates a random 30-byte SRTP Master Key and Salt, transmitting it securely over the TLS tunnel to the backend.
4. **SRTP Streaming:** The TLS socket is closed. OtterSec immediately spins up a GStreamer pipeline and begins transmitting hardware-encoded H.264 video over UDP (port `5004`), encrypted with the dynamically generated key using the AES-128-ICM cipher.
5. **Dynamic Decryption:** The backend receives the encrypted UDP packets, reads the stream's unique SSRC (Synchronization Source), and dynamically injects the negotiated key into its GStreamer decoder to render the feed.

---

## Utilization: C/C++ (Edge Device)

OtterSec operates as a shared library (`libottersec.so`) injected directly into your host edge application.

### 1. Configuration & Initialization
Configure the library dynamically, ensuring you pass the correct absolute or relative paths to your TLS certificates based on your application's execution environment.

```cpp
#include <ottersec.h>
#include <memory>

// Define the configuration
ottersec::OtterHandshakeConfig config;
config.handshake_port = 8443;
config.cert_path = "/path/to/certs/server.crt";
config.key_path = "/path/to/certs/server.key";

// Initialize the Session Manager
auto session_manager = std::make_unique<ottersec::SessionManager>(&config);
```

### 2. Triggering the Secure Stream

When the edge application detects an event, trigger the handshake. OtterSec handles the thread management and state transitions internally.

```cpp
ottersec::OtterFallEvent event;
session_manager->trigger_fall_event(&event);
```

Once triggered, OtterSec will wait for the Python backend to successfully connect and receive the keys before pushing GStreamer frames to the network.

## Utilization: Python (Backend Player)

The backend implementation serves as the stream viewer. It must first retrieve the keys via the out-of-band TLS socket, then initialize a GStreamer pipeline to decrypt the incoming UDP packets.

### 1. The TLS Client

Connect to the edge device to retrieve the 30-byte SRTP key payload.
```python

import socket
import ssl
import sys

EDGE_IP = '127.0.0.1'
EDGE_PORT = 8443

context = ssl.create_default_context()
context.check_hostname = False
context.verify_mode = ssl.CERT_NONE # Adjust for production CA verification

try:
    secure_socket = context.wrap_socket(socket.socket(socket.AF_INET, socket.SOCK_STREAM), server_hostname=EDGE_IP)
    secure_socket.connect((EDGE_IP, EDGE_PORT))
    payload = secure_socket.recv(30)
    secure_socket.close()
except Exception as e:
    print(f"TLS Error: {e}")
    sys.exit(1)
```

### 2. Dynamic SRTP Decryption via GStreamer

Because RTP multiplexes streams using an SSRC (Synchronization Source), the decryption key must be applied dynamically when the decoder requests it.

```python
import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst

Gst.init(None)

# 1. Set up the pipeline
pipeline = Gst.parse_launch(
    "udpsrc port=5004 ! application/x-srtp, payload=(int)96 ! "
    "srtpdec name=srtp_dec ! rtph264depay ! avdec_h264 ! videoconvert ! autovideosink"
)
srtp_dec = pipeline.get_by_name("srtp_dec")

# 2. Prepare the key buffer
key_buf = Gst.Buffer.new_allocate(None, 30, None)
key_buf.fill(0, payload)

# 3. Handle the dynamic key request
def on_request_key(srtpdec, ssrc):
    caps = Gst.Caps.from_string(
        f"application/x-srtp, ssrc=(uint){ssrc}, "
        "srtp-cipher=(string)aes-128-icm, srtp-auth=(string)hmac-sha1-80, "
        "srtcp-cipher=(string)aes-128-icm, srtcp-auth=(string)hmac-sha1-80, roc=(uint)0"
    )
    
    struct = caps.get_structure(0)
    struct.set_value("srtp-key", key_buf)
    struct.set_value("srtcp-key", key_buf)
    return caps

# 4. Bind the signal and play
srtp_dec.connect("request-key", on_request_key)
pipeline.set_state(Gst.State.PLAYING)
```

## Building and Installation
### Prerequisites
- C++17 compatible compiler (GCC/Clang)
- CMake >= 3.10
- OpenSSL (libssl-dev)
- GStreamer 1.0 (libgstreamer1.0-dev, libgstreamer-plugins-base1.0-dev)

### Compiling from Source
```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### Generating Development Certificates

For local testing, generate self-signed certificates in your host application's working directory:

```bash
openssl req -x509 -newkey rsa:4096 -keyout server.key -out server.crt -days 365 -nodes -subj "/CN=127.0.0.1"
```

For production, a service like LetsEncrypt or CertBot can be used.
