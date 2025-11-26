#include <Adafruit_NeoPixel.h>
#include <MIDI.h>

// === NeoPixel ===
#define PIN        12
#define NUMPIXELS  126   
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

HardwareSerial MIDI_SERIAL(2);

// Create MIDI interface using this serial port
MIDI_CREATE_INSTANCE(HardwareSerial, MIDI_SERIAL, MIDI);

// Choose your MIDI RX pin
#define MIDI_RX_PIN 16

#define KEY_SHIFT 36
#define FIRST_KEY 36
#define LAST_KEY 96

int listOfLEDsByKey[][3] = {
  {2,3,-1},{4,5,-1},{6,7,-1},{8,9,-1},{10,11,-1},{12,13,-1},
  {14,15,-1},{16,17,-1},{18,19,-1},{20,21,-1},{22,23,-1},{24,25,-1},
  {26,27,-1},{28,29,-1},{30,31,-1},{32,33,-1},{34,35,-1},{36,37,-1},
  {38,39,-1},{40,41,-1},{42,-1,-1},{43,44,45},{46,-1,-1},{47,48,-1},
  {49,50,-1},{51,52,-1},{53,54,-1},{55,56,-1},{57,58,-1},{59,60,-1},
  {61,62,-1},{63,64,-1},{65,66,-1},{67,68,-1},{69,70,-1},{71,-1,-1},
  {72,73,-1},{74,75,-1},{76,77,-1},{78,79,-1},{80,81,-1},{82,83,-1},
  {84,85,-1},{86,87,-1},{88,89,-1},{90,91,-1},{92,93,-1},{94,95,-1},
  {96,97,-1},{98,99,-1},{100,101,-1},{102,103,-1},{104,105,-1},
  {106,107,-1},{108,109,-1},{110,111,-1},{112,113,-1},{114,115,-1},
  {116,117,-1},{118,119,-1},{120,121,122},{123,124,125}
};

void lightLEDsByKey(int key, uint32_t color) {
  int index = key - KEY_SHIFT;
  if (index < 0 || index >= (sizeof(listOfLEDsByKey)/sizeof(listOfLEDsByKey[0]))) return;

  for (int i=0; i<3; i++) {
    int led = listOfLEDsByKey[index][i];
    if (led != -1) pixels.setPixelColor(led, color);
  }
  pixels.show();
}

// === CALLBACK: NOTE ON ===
void handleNoteOn(byte channel, byte note, byte velocity) {
  if (note >= FIRST_KEY && note <= LAST_KEY) {
    lightLEDsByKey(note, pixels.Color(0, 180, 0)); // ירוק
  }
}

// === CALLBACK: NOTE OFF ===
void handleNoteOff(byte channel, byte note, byte velocity) {
  if (note >= FIRST_KEY && note <= LAST_KEY) {
    lightLEDsByKey(note, 0); // כיבוי
  }
}

void setup() {
  Serial.begin(115200);
  pixels.begin();
  pixels.clear();
  pixels.show();

  // MIDI
  MIDI_SERIAL.begin(31250, SERIAL_8N1, MIDI_RX_PIN, -1);
  MIDI.begin(MIDI_CHANNEL_OMNI);
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);

  Serial.println("Ready. Press piano keys...");
}

void loop() {
  MIDI.read();
}