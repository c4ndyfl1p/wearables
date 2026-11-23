// NowReceiver.ino — ESP-NOW test receiver (DOIT ESP32 in the bag)
// Prints every message it receives from the sender.
// Arduino IDE: board "DOIT ESP32 DEVKIT V1"

#include <WiFi.h>
#include <esp_now.h>

// The message format. Must be IDENTICAL in the sender and the receiver.
struct Message {
  uint32_t count;
  float    value;
};

// Filled in by onReceive, printed by loop
volatile bool    newMessage = false;
Message          latest;
uint8_t          fromAddr[6];
uint32_t         lastCount = 0;
uint32_t         lost = 0;

// Runs automatically every time a message arrives.
// Kept short: it only copies the data; printing happens in loop().
void onReceive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  if (len != sizeof(Message)) return;          // ignore messages of the wrong size
  memcpy(&latest, data, sizeof(Message));
  memcpy(fromAddr, info->src_addr, 6);
  newMessage = true;
}

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);                         // radio on, but not joining any network
  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW failed to start");
    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);

  Serial.print("Receiver ready. My address: ");
  Serial.println(WiFi.macAddress());
  Serial.println("Waiting for messages...");
}

void loop() {
  if (newMessage) {
    newMessage = false;
    Message m = latest;                        // take a copy

    if (lastCount != 0 && m.count != lastCount + 1 && m.count > lastCount)
      lost += m.count - lastCount - 1;         // gaps in the count = lost messages
    lastCount = m.count;

    Serial.printf("from %02X:%02X:%02X:%02X:%02X:%02X  #%lu  value %.1f  (lost so far: %lu)\n",
                  fromAddr[0], fromAddr[1], fromAddr[2], fromAddr[3], fromAddr[4], fromAddr[5],
                  (unsigned long)m.count, m.value, (unsigned long)lost);
  }
}
