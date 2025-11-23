// Mega 2560: MIDI IN on RX1 (pin 19)

void setup() {
  Serial.begin(115200);    // USB to PC
  Serial1.begin(31250);    // MIDI in on pin 19 (RX1)
}

void loop() {
  while (Serial1.available() > 0) {
    byte b = Serial1.read();
    if (b < 0x10) Serial.print('0');  // leading zero
    Serial.print(b, HEX);
    Serial.print(' ');
  }
}