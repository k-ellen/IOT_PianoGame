// NeoPixel Ring simple sketch (c) 2013 Shae Erisson
// Released under the GPLv3 license to match the rest of the
// Adafruit NeoPixel library

#include <Adafruit_NeoPixel.h>
#ifdef __AVR__
 #include <avr/power.h> // Required for 16 MHz Adafruit Trinket
#endif

// Which pin on the Arduino is connected to the NeoPixels?
#define PIN        4 // On Trinket or Gemma, suggest changing this to 1

// How many NeoPixels are attached to the Arduino?
#define NUMPIXELS 123 // Popular NeoPixel ring size

// When setting up the NeoPixel library, we tell it how many pixels,
// and which pin to use to send signals. Note that for older NeoPixel
// strips you might need to change the third parameter -- see the
// strandtest example for more information on possible values.
Adafruit_NeoPixel pixels(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

#define DELAYVAL 500 // Time (in milliseconds) to pause between pixels
#define KEY_SHIFT 36
#define FIRST_KEY 36
#define LAST_KEY 96

int listOfLEDsByKey[][3] = {
  {2,3,-1},
  {4,5,-1},
  {6,7,-1},
  {8,9,-1},
  {10,11,-1},
  {12,13,-1},
  {14,15,-1},
  {16,17,-1},
  {18,19,-1},
  {20,21,-1},
  {22,23,-1},
  {24,25,-1},
  {26,27,-1},
  {28,29,-1},
  {30,31,-1},
  {32,33,-1},
  {34,35,-1},
  {36,37,-1},
  {38,39,-1},
  {40,41,-1},
  {42,-1,-1},
  {43,44,45},
  {46,-1,-1},
  {47,48,-1},
  {49,50,-1},
  {51,52,-1},
  {53,54,-1},
  {55,56,-1},
  {57,58,-1},
  {59,60,-1},
  {61,62,-1},
  {63,64,-1},
  {65,66,-1},
  {67,68,-1},
  {69,70,-1},
  {71,-1,-1},
  {72,73,-1},
  {74,75,-1},
  {76,77,-1},
  {78,79,-1},
  {80,81,-1},
  {82,83,-1},
  {84,85,-1},
  {86,87,-1},
  {88,89,-1},
  {90,91,-1},
  {92,93,-1},
  {94,95,-1},
  {96,97,-1},
  {98,99,-1},
  {100,101,-1},
  {102,103,-1},
  {104,105,-1},
  {106,107,-1},
  {108,109,-1},
  {110,111,-1},
  {112,113,-1},
  {114,115,-1},
  {116,117,-1},
  {118,119,-1},
  {120,121,122},
  {123,124,125}
};

void lightLEDsByKey(int key) {
  int index = key - KEY_SHIFT;

  auto leds = listOfLEDsByKey[index];  

  pixels.clear(); // Set all pixel colors to 'off'
  // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
  for(int i=0; i<3; i++) { // For each pixel...
    if(leds[i] != -1) {
      pixels.setPixelColor(leds[i], pixels.Color(0, 150, 0));
    }
  }

  pixels.show();   // Send the updated pixel colors to the hardware.
}


void setup() {
  // These lines are specifically to support the Adafruit Trinket 5V 16 MHz.
  // Any other board, you can remove this part (but no harm leaving it):
#if defined(__AVR_ATtiny85__) && (F_CPU == 16000000)
  clock_prescale_set(clock_div_1);
#endif
  // END of Trinket-specific code.

  pixels.begin(); // INITIALIZE NeoPixel strip object (REQUIRED)
}

void loop() {
  pixels.clear();

  for(int i=FIRST_KEY; i<=LAST_KEY; i++) { // For each pixel...
    pixels.clear(); // Set all pixel colors to 'off'
    // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
    lightLEDsByKey(i);

    delay(DELAYVAL); // Pause before next pass through loop
  }


  // pixels.clear(); // Set all pixel colors to 'off'
  // //The first NeoPixel in a strand is #0, second is 1, all the way up
  // // to the count of pixels minus one.
  // for(int i=0; i+1<NUMPIXELS; i+=2) { // For each pixel...
  //   pixels.clear(); // Set all pixel colors to 'off'
  //   // pixels.Color() takes RGB values, from 0,0,0 up to 255,255,255
  //   // Here we're using a moderately bright green color:
  //   pixels.setPixelColor(i, pixels.Color(0, 150, 0));
  //   pixels.setPixelColor(i+1, pixels.Color(0, 150, 0));

  //   pixels.show();   // Send the updated pixel colors to the hardware.

  //   delay(DELAYVAL); // Pause before next pass through loop
  // }
}
