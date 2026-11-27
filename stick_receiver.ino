#include <WiFi.h>
#include <esp_now.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// ESP-NOW callback only copies packets into this queue. USB serial output is
// handled by loop(), so a busy serial link cannot hold up radio reception.
static const uint32_t SERIAL_BAUD = 230400;
static const size_t PACKET_BUFFER_SIZE = 128;
static const UBaseType_t PACKET_QUEUE_LENGTH = 32;

struct ReceivedPacket {
  uint8_t length;
  uint8_t bytes[PACKET_BUFFER_SIZE];
};

QueueHandle_t receivedPackets = nullptr;

void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  (void)info;
  if (receivedPackets == nullptr || len <= 0 || len >= PACKET_BUFFER_SIZE) return;

  ReceivedPacket packet;
  packet.length = static_cast<uint8_t>(len);
  memcpy(packet.bytes, data, len);
  // Drop only if the queue is full; never block the ESP-NOW callback.
  xQueueSend(receivedPackets, &packet, 0);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1000);

  receivedPackets = xQueueCreate(PACKET_QUEUE_LENGTH, sizeof(ReceivedPacket));
  if (receivedPackets == nullptr) {
    Serial.println("Failed to create ESP-NOW receive queue");
    while (true) delay(1000);
  }

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);

  Serial.println("Receiver ready. Waiting for data from the stick...");
}

void loop() {
  ReceivedPacket packet;
  if (xQueueReceive(receivedPackets, &packet, portMAX_DELAY) == pdTRUE) {
    Serial.write(packet.bytes, packet.length);
    Serial.write('\n');
  }
}
