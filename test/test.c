#include "ottersec/api.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

void simulate_backend_connection() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
    return;

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(8443);

  inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr);

  if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0) {
    printf("[C Test] Mock connection sent.\n");
  } else {
    printf("[C Test] Failed to connect.\n");
  }
  close(sock);
}

int main() {
  printf("=== 1. Initializing OtterSec ===\n");
  OtterHandshakeConfig config = {0};
  config.handshake_port = 8443;
  config.auth_timeout_ms = 5000;

  if (ottersec_init(&config) != 0) {
    printf("Failed to initialize OtterSec.\n");
    return -1;
  }

  printf("\n=== 2. AI Detects Fall -> Trigger Protocol ===\n");
  OtterFallEvent event = {0};
  event.fall_detected = 1;
  event.confidence = 0.98f;
  ottersec_notify_fall(&event);

  printf("\n=== 3. Simulating Backend Network Connection ===\n");
  usleep(500000);
  simulate_backend_connection();

  printf("\n=== 4. Pushing GPU Frame to Pipeline ===\n");
  usleep(500000);
  OtterFrameBuffer frame = {0};
  frame.frame_num = 1;
  frame.width = 1920;
  frame.height = 1080;
  ottersec_push_frame(&frame);
  printf("[C Test] Frame pushed.\n");

  printf("\n=== 5. Teardown & Security Scrub ===\n");
  ottersec_terminate();
  printf("[C Test] Test complete.\n");

  return 0;
}
