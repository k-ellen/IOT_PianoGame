

#include <MIDI.h>
#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

#define PIXEL_PIN  2  // Digital IO pin connected to the NeoPixels.

#define PIXEL_COUNT 3 

#define DELAYVAL 500

Adafruit_NeoPixel pixels(PIXEL_COUNT, PIXEL_PIN, NEO_GRB + NEO_KHZ800);
// Use Serial1 for MIDI-IN (pins 19=RX, 18=TX, but TX not used for MIDI-IN)
MIDI_CREATE_INSTANCE(HardwareSerial, Serial1, MIDI);

const char* noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void printNoteName(byte note) {
    byte octave = (note /12) -1;
    byte index = note % 12;

    Serial.println(noteNames[index]);
    // Serial.print(octave);
}

void handleNoteOn(byte channel, byte note, byte velocity) {
    pixels.clear(); // Set all pixel colors to 'off'


    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    // Here we're using a moderately bright green color:
    pixels.setPixelColor(0, pixels.Color(0, 189, 0));
    pixels.setPixelColor(1, pixels.Color(0, 189, 0));
    pixels.setPixelColor(2, pixels.Color(0, 189, 0));

    pixels.show();   // Send the updated pixel colors to the hardware.


    Serial.print("Note On: ");
    printNoteName(note);
    // Serial.println(note);
}

void handleNoteOff(byte channel, byte note, byte velocity) {
    pixels.clear(); 
    pixels.show();
    Serial.print("Note Off: ");
    printNoteName(note);
    // Serial.println(note);
}

void setup() {
    #if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
        clock_prescale_set(clock_div_1);
    #endif

    pixels.begin();

    Serial.begin(115200);       // Debug output
    Serial1.begin(31250);       // MIDI baud
    MIDI.begin(MIDI_CHANNEL_OMNI);  // Listen to all channels
    MIDI.turnThruOff();

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



// #include <MIDI.h>
// // #include <SoftwareSerial.h>
// #include <AltSoftSerial.h>

// // SoftwareSerial midiSerial(2, 3); // RX, TX
// AltSoftSerial midiSerial;  // RX=8, TX=9 (TX not used)

// MIDI_CREATE_INSTANCE(AltSoftSerial, midiSerial, MIDI);

// void handleNoteOn(byte channel, byte note, byte velocity) {
//   Serial.println("note on");
// }

// void handleNoteOff(byte channel, byte note, byte velocity) {
//   Serial.println("note off");
// }


// void setup()
// {
//   // Open serial communications and wait for port to open:
//   Serial.begin(115200);
//   Serial.println("hardware serial ready");

//     // Start MIDI
//   midiSerial.begin(31250);
//   MIDI.begin(MIDI_CHANNEL_OMNI);

//   // Set callbacks
//   MIDI.setHandleNoteOn(handleNoteOn);
//   MIDI.setHandleNoteOff(handleNoteOff);

//   Serial.println("MIDI ready...");
// }

// void loop() // run over and over
// {
//   MIDI.read();
// }


