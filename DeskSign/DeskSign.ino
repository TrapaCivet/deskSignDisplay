#include <FastLED.h>
#include <Button2.h>

// How many leds in your strip?
#define NUM_LEDS 6 

// For led chips like Neopixels, which have a data line, ground, and power, you just
// need to define DATA_PIN.  For led chipsets that are SPI based (four wires - data, clock,
// ground, and power), like the LPD8806, define both DATA_PIN and CLOCK_PIN
#define DATA_PIN 11
#define CLOCK_PIN 13

// This needs to be a INTERRUPT pin!
#define BUTTON_PIN 2
Button2 buttonA = Button2(BUTTON_PIN);


// Define the array of leds
CRGB leds[NUM_LEDS];

// A CRGB object that will be used to store destination colour.
CRGB endCol;
CRGB color2;
// A int to represent the button state
volatile int buttonState = 0;

// Int for representing what step within a fade we're at
int stepsRemaining = 0;
uint8_t stepSize = 0;
int sleepSize = 0;
int sleepRemain = 0;



// Mode
volatile uint8_t masterMode = 0;
uint8_t subMode = 0;
volatile bool interruptFlag = 0;






// Fire2012 by Mark Kriegsman, July 2012
// as part of "Five Elements" shown here: http://youtu.be/knWiGsmgycY
//// 
// This basic one-dimensional 'fire' simulation works roughly as follows:
// There's a underlying array of 'heat' cells, that model the temperature
// at each point along the line.  Every cycle through the simulation, 
// four steps are performed:
//  1) All cells cool down a little bit, losing heat to the air
//  2) The heat from each cell drifts 'up' and diffuses a little
//  3) Sometimes randomly new 'sparks' of heat are added at the bottom
//  4) The heat from each cell is rendered as a color into the leds array
//     The heat-to-color mapping uses a black-body radiation approximation.
//
// Temperature is in arbitrary units from 0 (cold black) to 255 (white hot).
//
// This simulation scales it self a bit depending on NUM_LEDS; it should look
// "OK" on anywhere from 20 to 100 LEDs without too much tweaking. 
//
// I recommend running this simulation at anywhere from 30-100 frames per second,
// meaning an interframe delay of about 10-35 milliseconds.
//
// Looks best on a high-density LED setup (60+ pixels/meter).
//
//
// There are two main parameters you can play with to control the look and
// feel of your fire: COOLING (used in step 1 above), and SPARKING (used
// in step 3 above).
//
// COOLING: How much does the air cool as it rises?
// Less cooling = taller flames.  More cooling = shorter flames.
// Default 50, suggested range 20-100 
#define COOLING  55

// SPARKING: What chance (out of 255) is there that a new spark will be lit?
// Higher chance = more roaring fire.  Lower chance = more flickery fire.
// Default 120, suggested range 50-200.
#define SPARKING 120

#define FRAMES_PER_SECOND 60
bool gReverseDirection = false;









void setup() { 
  buttonA.setClickHandler(buttonHandler);
  buttonA.setLongClickHandler(buttonHandler);
  buttonA.setDoubleClickHandler(buttonHandler);
  buttonA.setTripleClickHandler(buttonHandler);
  

  // Set up Serial Debugging
  Serial.begin(115200);
  Serial.println("Starting Script");

  
  //FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.addLeds<WS2812, DATA_PIN, GRB>(leds, NUM_LEDS);
  for (int i = 0; i < NUM_LEDS; i++){
    leds[i] = CRGB::Blue;
  }
  FastLED.show();
}



// Blend one CRGB color toward another CRGB color by a given amount.
// Blending is linear, and done in the RGB color space.
// This function modifies 'cur' in place.
    CRGB fadeTowardColor( CRGB& cur, const CRGB& target, uint8_t amount){
      nblendU8TowardU8( cur.red,   target.red,   amount);
      nblendU8TowardU8( cur.green, target.green, amount);
      nblendU8TowardU8( cur.blue,  target.blue,  amount);
      return cur;
    }

// Helper function that blends one uint8_t toward another by a given amount
    void nblendU8TowardU8( uint8_t& cur, const uint8_t target, uint8_t amount){
      if( cur == target) return;
      if( cur < target ) {
        uint8_t delta = target - cur;
        delta = scale8_video( delta, amount);
        cur += delta;
      } else {
        uint8_t delta = cur - target;
        delta = scale8_video( delta, amount);
        cur -= delta;
      }
    }

/*
 *This function performs a fade between the current LED colour in the strip, and a destination colour. 
 *  you specify how long this is to take and it handles the process.
 */
void calcFade(int duration){
    Serial.print("Calc fade for duration: "); Serial.println(duration);
    // Our input is duration. How long this fade will take.
    // but our fades can take a maximum of 255 steps. So in reality our duration calculation is really
    // how long our wait is.
    if (duration == 0){
      // They want it instant, okay.
      //Serial.println("Duration is 0");
      sleepSize = 0;
      stepSize = 255;
    } else if (duration <= 255){
      //Serial.println("Duration is 255 or less");
      // Duration is FASTER than the number of steps
      sleepSize = 0;
      stepSize = duration;
    } else {
      //Serial.println("Duration is more than 255");
      sleepSize = duration / 255;
      stepSize = 1;
    }
    stepsRemaining = 255;
}
    
    
void doFadeStep(CRGB destinationColour, int debug) {
    // If we have any sleep time remaining, we'll do that INSTEAD of changing our colour.
    if (sleepRemain > 0){
       //Serial.print("SleepRemain:"); Serial.println(sleepRemain);
       if(sleepRemain <= 50){
          delay(sleepRemain);
          if (debug == 1){ Serial.print("doFadeStep::Sleeeping "); Serial.print(sleepRemain); Serial.println(" Removing the remaining amount of sleep"); }
          sleepRemain = 0;
       } else {
          delay(50);
          sleepRemain = sleepRemain - 50;
          if (debug == 1) { Serial.print("doFadeStep::Sleeping 50, now we have "); Serial.print(sleepRemain); Serial.println(" sleep remaining."); }
       }
       return;
    }
    // So we are doing a single step in our fade, First, we need to determine if the sleep timer is 
    //  currently set.
    if(stepsRemaining > 0){
      if (debug == 1){ 
         Serial.print("doFadeStep::stepSize:"); Serial.print(stepSize); 
         Serial.print(" sleepRemain:"); Serial.print(sleepRemain);
         Serial.print(" stepsRemaining:"); Serial.print(stepsRemaining); 
         Serial.print(" sleepSize:"); Serial.println(sleepSize); 
      }
      // Okay we're not sleeping, SO, we need to take a step.
         for(int z=0; z< NUM_LEDS; z++){
            fadeTowardColor(leds[z], destinationColour, stepSize);
         }
         FastLED.show();
         // And set up sleepRemain to delay the proper amount of time.
         sleepRemain = sleepSize; 
         stepsRemaining = stepsRemaining - stepSize;
         if (stepsRemaining < 0){ stepsRemaining = 0; }
    } else if (stepsRemaining == 0){
       if (debug == 1){ Serial.println("   DoneFade"); }
         // We are done this fade. We should reset things
         stepSize = 0;
         sleepSize = 0; 
    }
    
    
}







void staticColour(CRGB destinationColour) { 
  if (subMode == 99){
    delay(50);
  } else {
    for(int i=0; i < NUM_LEDS; i++){ 
      leds[i] = destinationColour; 
    }
    FastLED.show();
    subMode = 99;
  }
}



void runMode1(int debug){
  if (debug==1){
  Serial.print("RunMode1::StepsRemain:"); Serial.print(stepsRemaining); Serial.print(" sleepRemain:"); Serial.print(sleepRemain);
  Serial.print(" stepSize:"); Serial.print(stepSize); Serial.print(" subMode:"); Serial.println(subMode);
  }

  if (stepSize == 0){
    // We have not yet started a fade. 
    //   so we will set our colour and calcualate our fade.
    if(subMode == 0){
       if (debug == 1){ Serial.println("Submode 0"); }
       // We are on submode 0
       endCol.setRGB(50,255,50);
       calcFade(6000);
       subMode = 1;
    } else if (subMode == 1) {
       if (debug == 1){ Serial.println("Submode 1 - Delay"); }
       calcFade(3000);
       subMode = 2;
    } else if (subMode == 2) {
      if (debug == 1){ Serial.println("Submode 2"); }
       endCol.setRGB(30,0,0);
       calcFade(3000);
       subMode = 3;
    } else if (subMode == 3) {
      if (debug == 1){ Serial.println("Submode 3 - Delay"); }
       calcFade(1500);
       subMode = 0;
    } else {
       subMode = 0;
    }
  }
  // We have started a fade or we are in the middle of a fade.
  doFadeStep(endCol, debug);
}


void runMode0(int debug){
  if (debug==1){
  Serial.print("RunMode0::StepsRemain:"); Serial.print(stepsRemaining); Serial.print(" sleepRemain:"); Serial.print(sleepRemain);
  Serial.print(" stepSize:"); Serial.print(stepSize); Serial.print(" subMode:"); Serial.println(subMode);
  }
  
  if (stepSize == 0){
    // We have not yet started a fade. 
    //   so we will set our colour and calcualate our fade.
    if(subMode == 0){
       if (debug == 1){ Serial.println("Submode 0"); }
       // We are on submode 0
       endCol.setRGB(0,0,255);
       calcFade(3000);
       subMode = 1;
    } else if (subMode == 1) {
       if (debug == 1){ Serial.println("Submode 1 - Delay"); }
       calcFade(1500);
       subMode = 2;
    } else if (subMode == 2) {
      if (debug == 1){ Serial.println("Submode 2"); }
       endCol.setRGB(0,255,0);
       calcFade(3000);
       subMode = 3;
    } else if (subMode == 3) {
      if (debug == 1){ Serial.println("Submode 3 - Delay"); }
       calcFade(1500);
       subMode = 4;
    } else if (subMode == 4){
      if (debug == 1){  Serial.println("Submode 4"); }
       endCol.setRGB(255,0,0);
       calcFade(3000);
       subMode = 5;
    } else if (subMode == 5) {
      if (debug == 1){ Serial.println("Submode 5 - Delay"); }
       calcFade(1500);
       subMode = 6;
    } else if (subMode == 6){
       if (debug == 1){  Serial.println("Submode 6"); }
       endCol.setRGB(75,75,0);
       calcFade(3000);
       subMode = 0;
    } else {
       subMode = 0;
    }
  } 
  // We have started a fade or we are in the middle of a fade.
  doFadeStep(endCol, debug);
}



/*
 *   FIRE SIMULATOR
 * 
 */

void Fire2012(){
  // Array of temperature readings at each simulation cell
  static byte heat[NUM_LEDS];

  // Step 1.  Cool down every cell a little
    for( int i = 0; i < NUM_LEDS; i++) {
      heat[i] = qsub8( heat[i],  random8(0, ((COOLING * 10) / NUM_LEDS) + 2));
    }
  
    // Step 2.  Heat from each cell drifts 'up' and diffuses a little
    for( int k= NUM_LEDS - 1; k >= 2; k--) {
      heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2] ) / 3;
    }
    
    // Step 3.  Randomly ignite new 'sparks' of heat near the bottom
    if( random8() < SPARKING ) {
      int y = random8(7);
      heat[y] = qadd8( heat[y], random8(160,255) );
    }

    // Step 4.  Map from heat cells to LED colors
    for( int j = 0; j < NUM_LEDS; j++) {
      CRGB color = HeatColor( heat[j]);
      int pixelnumber;
      if( gReverseDirection ) {
        pixelnumber = (NUM_LEDS-1) - j;
      } else {
        pixelnumber = j;
      }
      leds[pixelnumber] = color;
    }
}



void LightingEffect(CRGB generalColour, CRGB flashColour) {
    int flashCount;

    // If we have any sleep time remaining, we'll do that INSTEAD of changing our colour.
    if (sleepRemain > 0){
       //Serial.print("SleepRemain:"); Serial.println(sleepRemain);
       if(sleepRemain <= 50){
          delay(sleepRemain);
          sleepRemain = 0;
       } else {
          delay(50);
          sleepRemain = sleepRemain - 50;
       }
       return;
    }


    if(stepsRemaining == 1){
        // Set everything to the normal colour
        for (int i = 0; i < NUM_LEDS; i++){
          leds[i] = generalColour;
        }
        FastLED.show();
        // Now set it up to sleep for a while before doing lightning
        sleepRemain = random(10000);
        stepsRemaining = 2;
    } else if (stepsRemaining == 2){
      // FLASH  - (First of up to three)
          for (int i = 0; i < NUM_LEDS; i++){
            leds[i] = flashColour;
          }
          FastLED.show();
          
          // Set up the amount of time it'll be on.
          sleepRemain = random(3)*40; // <1 - 3> * 20 = <20 to 60 delay>
    
          // But next we need to determine how many flashes this lighting round will have.
          flashCount = random(2);
    
          if (flashCount == 2) {
            // We want to do 3 flashes. 
            stepsRemaining = 3; // Jump to step five (3-flash,4-gap,5-gap,6-flash,7-gap) 
          } else if (flashCount == 1) { 
            // We want to do 2 flashes
            stepsRemaining = 5; // Jump to step five (5-gap,6-flash,7-gap) 
          } else {
            // Then we set the next stpe to ?;
            stepsRemaining = 1; // Only 1 flash will happen.
          }
    } else if (stepsRemaining == 3){
      // GAP - (From here 3 flashes will remain.)
          // Set everything to the normal colour
            for (int i = 0; i < NUM_LEDS; i++){
              leds[i] = generalColour;
            }
            FastLED.show();
          // Set the sleep remain as the gap between the flashes.
          sleepRemain = 60;
          // And onto the next step.
          stepsRemaining = 4;
    } else if (stepsRemaining == 4){
      // FLASH (From here 2 flashes will remain.)
          // We're going to flash now (First Flash)
          for (int i = 0; i < NUM_LEDS; i++){
            leds[i] = flashColour;
          }
          FastLED.show();
          // Set the sleep remain as the gap between the flashes
          sleepRemain = 60;
          // And onto the next step.
          stepsRemaining = 5;
    } else if (stepsRemaining == 5){
      // GAP - (From here 2 flashes will remain)
          // Set everything to the normal colour
            for (int i = 0; i < NUM_LEDS; i++){
              leds[i] = generalColour;
            }
            FastLED.show();
          // Set the sleep remain as the gap between the flashes.
          sleepRemain = 60;
          // And onto the next step.
          stepsRemaining = 6;
    } else if (stepsRemaining == 6){
      // FLASH (LAST possible flash)
          // We're going to flash now (First Flash)
          for (int i = 0; i < NUM_LEDS; i++){
            leds[i] = flashColour;
          }
          FastLED.show();
          // Set the sleep remain as the gap between the flashes
          sleepRemain = 60;
          // And onto the next step.
          stepsRemaining = 7;
    } else if (stepsRemaining == 7){
      // GAP - (Last Gap)
          // Set everything to the normal colour
            for (int i = 0; i < NUM_LEDS; i++){
              leds[i] = generalColour;
            }
            FastLED.show();
          // Set the sleep remain as the gap between the flashes.
          sleepRemain = 60;
          // And onto the next step.
          stepsRemaining = 1;
    } else {
      // We probably came over from another step.
      sleepRemain = 0;
      stepsRemaining = 1;
    }
  
}



void SnowSparkle(CRGB generalColour, CRGB sparkleColour, int SparkleDelay, int SpeedDelay) {


    // If we have any sleep time remaining, we'll do that INSTEAD of changing our colour.
    if (sleepRemain > 0){
       //Serial.print("SleepRemain:"); Serial.println(sleepRemain);
       if(sleepRemain <= 50){
          delay(sleepRemain);
          sleepRemain = 0;
       } else {
          delay(50);
          sleepRemain = sleepRemain - 50;
       }
       return;
    }


    if(stepsRemaining == 1){
      // Set all the led's the generalColour
      for (int i = 0; i < NUM_LEDS; i++){
        leds[i] = generalColour;
      }
      FastLED.show();
      // Now wait a period before proceeding to the next step
      sleepRemain = SpeedDelay;
      stepsRemaining = 2;
    } else if (stepsRemaining == 2){
      // Now we're going to spark.
      // Choose a random pixel to blink
      int Pixel = random(NUM_LEDS);

      // Set that pixel the flicker colour
      leds[Pixel] = sparkleColour;
      FastLED.show();
      sleepRemain = SparkleDelay;
      stepsRemaining = 1;
    } else {
      // We probably came over from another step.
      sleepRemain = 0;
      stepsRemaining = 1;
    }
  
}











void loop() { 

  
  if (interruptFlag){
    //Serial.print("Mode:"); Serial.print(masterMode); Serial.println(":");
    interruptFlag = 0;
  }
  if (masterMode == 0){// Lightning effect
      endCol.setRGB(10,10,80);
      color2.setRGB(90,90,120);
      LightingEffect(endCol,color2);
       
  } else if (masterMode == 1){ // Snow Sparkle
      endCol.setRGB(10,10,80);
      color2.setRGB(90,90,120);
      SnowSparkle(endCol,color2,20,random(100,1000));
  } else if (masterMode == 2){ // Fire
      Fire2012(); // run simulation frame
      FastLED.show(); // display this frame
      FastLED.delay(1000 / 20); 
  } else if (masterMode == 3){ // RGB Fade
      runMode0(0);
  } else if (masterMode == 4){ // red
      endCol.setRGB(255,0,0);
      staticColour(endCol);  
  } else if (masterMode == 5){ // Fade
      runMode1(0);
  } else if (masterMode == 6) { // Light purple?
      endCol = CRGB::Indigo;
      staticColour(endCol);
  } else if (masterMode == 7) { // light Purple
      endCol = CRGB::Aquamarine;
      staticColour(endCol);
  } else if (masterMode == 8) { // Pink
      //endCol = CRGB::BlueViolet;
      endCol.setRGB(230,117,155);
      staticColour(endCol); 
  } else if (masterMode == 9) { // Green
      endCol.setRGB(0,255,12);
      staticColour(endCol);
  } else if (masterMode == 10) { // Off
      endCol.setRGB(0,0,0);
      staticColour(endCol);
      
  } else {
     masterMode = 0;
  }
  buttonA.loop();
}




void buttonHandler(Button2& btn) {
    switch (btn.getClickType()) {
        case SINGLE_CLICK:
            break;
        case DOUBLE_CLICK:
            Serial.print("double ");
            break;
        case TRIPLE_CLICK:
            Serial.print("triple ");
            break;
        case LONG_CLICK:
            Serial.print("long");
            break;
    }
    Serial.print("click");
    Serial.print(" (");
    Serial.print(btn.getNumberOfClicks());    
    Serial.println(")");
    Serial.print("New mode is: ");
    masterMode++;
    // This limiter code isn't needed beacuse of the else statment in the main loop. When
    // the counter proceeds past the maximum allowable limit it'll be overwritten to zero.
    /*if (masterMode > 10){
       masterMode = 0;
    }*/
    interruptFlag = 1;
    stepSize = 0;
    subMode = 0;
    Serial.println(masterMode);
}
