

#include <MIDI.h>

// Use Serial1 for MIDI-IN (pins 19=RX, 18=TX, but TX not used for MIDI-IN)
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

void handleNoteOn(byte channel, byte note, byte velocity) {
    Serial.print("Note On: ");
    Serial.println(note);
}

void handleNoteOff(byte channel, byte note, byte velocity) {
    Serial.print("Note Off: ");
    Serial.println(note);
}

void setup() {
    Serial.begin(115200);       // Debug output
    Serial1.begin(31250);       // MIDI baud
    MIDI.begin(MIDI_CHANNEL_OMNI);  // Listen to all channels

    MIDI.setHandleNoteOn(handleNoteOn);
    MIDI.setHandleNoteOff(handleNoteOff);

    Serial.println("MIDI ready on Mega with HardwareSerial");
}

void loop() {
    MIDI.read();  // Always call as often as possible
    // if (MIDI.read()) {
    //     Serial.print("Type: "); Serial.print(MIDI.getType());
    //     Serial.print(" Channel: "); Serial.print(MIDI.getChannel());
    //     Serial.print(" Note: "); Serial.print(MIDI.getData1());
    //     Serial.print(" Velocity: "); Serial.println(MIDI.getData2());
    // }
}