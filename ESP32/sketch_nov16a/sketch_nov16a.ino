#include <MIDI.h>

// If using Uno/Nano, use Serial: MIDI_CREATE_INSTANCE(HardwareSerial, Serial, MIDI);
// If using Mega (which has Serial1), use Serial1 (pins 19/18):
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI); 


void handleNoteOn(byte channel1, byte note1, byte velocity1) {
    // Serial.print("Note On: ");
    // Serial.println(note);
    byte type = MIDI.getType();
    byte channel = MIDI.getChannel();
    byte data1 = MIDI.getData1(); // Note Number
    byte data2 = MIDI.getData2(); // Velocity

    // --- Print MIDI Type/Status in Hex ---
    Serial.print("Type: 0x"); 
    Serial.print(type, HEX); 
        
    Serial.print(" Ch: "); 
    Serial.print(channel);
        
    // --- Print Data 1 (Note) in Decimal AND Hex ---
    Serial.print(" D1 (Note): "); 
    Serial.print(data1);            // Decimal
    Serial.print(" (0x");
    Serial.print(data1, HEX);       // Hexadecimal
    Serial.print(")");
        
    // --- Print Data 2 (Velocity) in Decimal AND Hex ---
    Serial.print(" D2 (Vel): "); 
    Serial.print(data2);            // Decimal
    Serial.print(" (0x");
    Serial.print(data2, HEX);       // Hexadecimal
    Serial.println(")");
}

// void handleNoteOff(byte channel, byte note, byte velocity) {
//     Serial.print("Note Off: ");
//     Serial.println(note);
// }

void setup() {
    Serial.begin(115200);       // Debug output
    Serial1.begin(31250);       // MIDI baud
    MIDI.begin(MIDI_CHANNEL_OMNI);  // Listen to all channels

    MIDI.setHandleNoteOn(handleNoteOn);
    // MIDI.setHandleNoteOff(handleNoteOff);

    Serial.println("MIDI ready on Mega with HardwareSerial");
}


void loop() {
    // Check for a new MIDI message
    MIDI.read();

}
