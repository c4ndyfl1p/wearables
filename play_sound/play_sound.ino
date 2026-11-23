const int DAC_PIN = 25;

void setup() {}

void loop() {
  // 440 Hz test tone
  // dacWrite(DAC_PIN, 200); delayMicroseconds(1136);
  // dacWrite(DAC_PIN, 55);  delayMicroseconds(1136);



//  dacWrite(DAC_PIN, 140); delayMicroseconds(1136);
// dacWrite(DAC_PIN, 115); delayMicroseconds(1136);

 dacWrite(DAC_PIN, 129); delayMicroseconds(1136);
dacWrite(DAC_PIN, 125); delayMicroseconds(1136);


}