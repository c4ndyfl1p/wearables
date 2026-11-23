#include <WiFi.h>
#include <esp_now.h>

// Runs every time a message from the stick arrives
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  Serial.write(data, len);   // print the message
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA);       // turn on the radio
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
    while (1) delay(10);
  }
  esp_now_register_recv_cb(onReceive);

  Serial.println("Receiver ready. Waiting for data from the stick...");
}

void loop() {
  // nothing here - receiving happens in onReceive()
}