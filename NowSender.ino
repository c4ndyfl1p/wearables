// NowSender.ino — ESP-NOW test sender (ESP32-C3 SuperMini on the staff)
// Broadcasts a message 10 times per second to any ESP nearby.
// Arduino IDE: board "ESP32C3 Dev Module", Tools -> USB CDC On Boot -> Enabled

#include <WiFi.h>
#include <esp_now.h>

// The message format. Must be IDENTICAL in the sender and the receiver.
struct Message {
  uint32_t count;   // counts up by 1 each message, to spot lost messages
  float    value;   // test value (will become the spin rate later)
};

uint8_t broadcastAddr[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};   // "everyone"
Message msg = {0, 0};

void setup() {
  Serial.begin(115200);
  delay(1500);

  WiFi.mode(WIFI_STA);                       // radio on, but not joining any network
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
    while (true) delay(1000);
  }

  esp_now_peer_info_t peer = {};             // register "everyone" as a destination
  memcpy(peer.peer_addr, broadcastAddr, 6);
  peer.channel = 0;                          // 0 = use the current channel
  peer.encrypt = false;
  if (esp_now_add_peer(&peer) != ESP_OK) {
    Serial.println("Could not add broadcast peer");
    while (true) delay(1000);
  }

  Serial.print("Sender ready. My address: ");
  Serial.println(WiFi.macAddress());
}

void loop() {
  msg.count++;
  msg.value = 500.0f * sinf(millis() / 1000.0f);   // fake spin rate, swings -500..500

  esp_err_t result = esp_now_send(broadcastAddr, (uint8_t*)&msg, sizeof(msg));
  Serial.printf("sent #%lu  value %.1f  %s\n",
                (unsigned long)msg.count, msg.value, result == ESP_OK ? "ok" : "ERROR");

  delay(100);
}
