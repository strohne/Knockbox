/* KNOCKBOX


   Detects patterns of knocks and triggers a servo to unlock
   a door if the pattern is correct. Feedback is signaled by
   a LED strip.
   
   By Jakob Jünger
   Version 1.0
   Licensed under Creative Commons Attribution-Noncommercial-Share Alike 3.0
   http://creativecommons.org/licenses/by-nc-sa/3.0/us/
   (In short: Do what you want, just be sure to include this line and the four above it, and don't sell it or use it in anything you sell without contacting me.)
 
 */
      
 
/* Validating a knock is based on:

   By Steve Hoefer http://grathio.com
   Version 0.1.10.20.10
   Licensed under Creative Commons Attribution-Noncommercial-Share Alike 3.0
   http://creativecommons.org/licenses/by-nc-sa/3.0/us/
   (In short: Do what you want, just be sure to include this line and the four above it, and don't sell it or use it in anything you sell without contacting me.)
*/

/* WIRING
  
   Analog Pin 0: Piezo speaker (connected to ground with 1M pulldown resistor)
   Digital Pin 7: Switch to enter a new code.  Short this to enter programming mode.
   Digital Pin 8: Servo
   Digital Pin 2: Red LED. 
   Digital Pin 3: Blue  LED. 
   Digital Pin 4: LED strip. 
*/
 
//Servo, used to turn the lock
#include <Servo.h>
#define PIN_SERVO 8
Servo doorServo;

//Memory, used to store the code
#include <EEPROM.h>

//Light to signal feedback
#include "FastLED.h"
#define LEDS_NUM 30
CRGB leds[LEDS_NUM];

#define LEDS_TYPE WS2812B
#define LEDS_COLOR_ORDER GRB
#define LEDS_BRIGHTNESS 255
#define LEDS_UPDATES_PER_SECOND 100

#define PIN_LED_RED 2
#define PIN_LED_BLUE 3
#define PIN_LED_STRIP 4


// Sensors for knock detection
#define PIN_SENSOR_KNOCK A0  // Piezo sensor on pin 0.
#define PIN_SENSOR_SWITCH 7  // Switch to program new code

 
// Tuning constants for knock detection
const int knock_threshold = 5;           // Minimum signal from the piezo to register as a knock
const int knock_rejectValue = 25;        // If an individual knock is off by this percentage of a knock we don't unlock..
const int knock_averageRejectValue = 15; // If the average timing of the knocks is off by this percent we don't unlock.

// Knock pattern
const int maximumKnocks = 20;       // Maximum number of knocks to listen for.
byte secretCode[maximumKnocks] = {100, 50, 100, 50, 50, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // Initial setup: "Tap - Tap Tap - Tap Tap Tap Tap"
int knockReadings[maximumKnocks];   // When someone knocks this array fills with delays between knocks.
int value_knockSensor = 0;           // Last reading of the knock sensor.


//States
#define KNOCK_IDLE 0
#define KNOCK_STARTED 1
#define KNOCK_KNOCKING 2
#define KNOCK_FINISHED 3
#define KNOCK_FAILED_STARTED 4
#define KNOCK_FAILED 5
#define KNOCK_SUCCESS_STARTED 6
#define KNOCK_SUCCESS 7
#define KNOCK_STORED 8

boolean state_ledsUpdated = true;
boolean state_programming = false;
boolean state_programSwitchChanged = false;
boolean state_programSwitch = true;
boolean state_doorOpen = false;
boolean state_servoAttached = false;
int state_knocking = KNOCK_IDLE;
boolean state_knocked = false;

//Timers
unsigned long timer_knockDebounce = 0;
unsigned long timer_knockFinished = 0;
unsigned long timer_servoStop = 0;
unsigned long timer_ledKnock = 0;
unsigned long timer_feedbackMode = 0;

const int time_knockDebounce = 150;   // Milliseconds we allow a knock to fade before we listen for another one (debounce timer).
const int time_servoStop = 1000;      // Milliseconds that we give the servo to reach position.
const int time_knockFinished = 1700;  // Longest time to wait for a knock before we assume that it's finished.
const int time_ledKnock = 200;   
const int time_feedbackMode = 4000;   
const int time_feedbackModeRestart = 1000;   


void setup() { 
  //Position servo
  delay(500);
  moveServo(0);
  delay(500);
  stopServo();         
    
  //Configure pins  
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_SENSOR_SWITCH, INPUT);
  digitalWrite(PIN_SENSOR_SWITCH, HIGH);      
  
  //Leds
  FastLED.addLeds<LEDS_TYPE,PIN_LED_STRIP,LEDS_COLOR_ORDER>(leds, LEDS_NUM).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  LEDS_BRIGHTNESS );
  fill_solid(leds, LEDS_NUM, CRGB::Black);  
  //FastLED.show();
  
  //Load knock pattern
  for (int i=0;i<maximumKnocks;i++){
    secretCode[i]= EEPROM.read(i);
  }
      
  //Ready
  digitalWrite(PIN_LED_BLUE, HIGH);   
  digitalWrite(PIN_LED_RED, HIGH);     
  delay(time_ledKnock);
  digitalWrite(PIN_LED_BLUE, LOW);    
  digitalWrite(PIN_LED_RED, LOW);      
}

void loop() {
  
  //Check program button
  if (digitalRead(PIN_SENSOR_SWITCH) != state_programSwitch) {      
      state_programSwitch = !state_programSwitch;
      state_programSwitchChanged = true;
      if (!state_programSwitch) state_programming = true; //pullup -> pulldown
      //timer_knockDebounce = millis();
  } 
  
  // Listen for knocks
  if (timerFinished(timer_knockDebounce,time_knockDebounce)) {
    value_knockSensor = analogRead(PIN_SENSOR_KNOCK);
    state_knocked = (value_knockSensor > knock_threshold);
    if (state_knocked) {
       timerStart(timer_knockDebounce);
    
      //Break feedback
      if ((state_knocking == KNOCK_FAILED) &&
          (timerFinished(timer_feedbackMode,time_feedbackModeRestart)))
      {
        state_knocking = KNOCK_IDLE;
        writeBlack();
      }    
       
    }   
  }  
  recordKnockPattern();
  
  
  //Process knock pattern
  if ((state_knocking == KNOCK_FINISHED) && state_programming) {
    storeLock();
    state_programming = false;
    state_knocking = KNOCK_STORED; //needs to be cleared at end of loop
  }

  else if ((state_knocking == KNOCK_FINISHED) && !state_programming) {
    if (validateKnock())
      state_knocking = KNOCK_SUCCESS_STARTED; //needs to be cleared at end of loop
    else state_knocking = KNOCK_FAILED_STARTED; //needs to be cleared at end of loop
  }
  

  
  //Servo feedback
  if (state_knocking == KNOCK_STORED) {
    moveServo(0);
    state_doorOpen=false;
  }

  else if (state_knocking == KNOCK_SUCCESS_STARTED) {
    state_doorOpen = !state_doorOpen; 
    moveServo(map(state_doorOpen,0,1,0,180));  
    //Serial.println(state_doorOpen);
  }
  
  if (timerFinished(timer_servoStop,time_servoStop)) {
    stopServo();  
  }  

  //LED feedback
  //-Programming
  if ((state_knocking == KNOCK_STORED) || state_programSwitchChanged) {
    digitalWrite(PIN_LED_RED, state_programming);     
  } 
   
  //-Knock
  if  (state_knocking == KNOCK_STARTED) {
    fill_solid(leds, LEDS_NUM, CRGB::Black);
  }
  
  if  (((state_knocking == KNOCK_STARTED) || (state_knocking == KNOCK_KNOCKING) )  && (state_knocked)) {
    writePulse();
    digitalWrite(PIN_LED_BLUE, HIGH);
    timerStart(timer_ledKnock);
  }
  
  if (timerFinished(timer_ledKnock,time_ledKnock))
    digitalWrite(PIN_LED_BLUE, LOW);          

  //Animate LED Strip
  if (state_knocking == KNOCK_KNOCKING) {
    writeMove();
  }  else if (state_knocking == KNOCK_SUCCESS) {
    writeSuccess();
  }  else if (state_knocking == KNOCK_FAILED) {
    writeFail();
  }  else if (state_knocking == KNOCK_IDLE) {
    writeKeepAlive();
  }  
  
  updateLEDFrame();

  //Advance states
  if (state_knocking == KNOCK_SUCCESS_STARTED)
  {
    state_knocking = KNOCK_SUCCESS;
    timerStart(timer_feedbackMode);
  }  
  else if (state_knocking == KNOCK_FAILED_STARTED)
  {
    state_knocking = KNOCK_FAILED;
    timerStart(timer_feedbackMode);
  } 
  
  else if (state_knocking == KNOCK_STARTED)
  {
    state_knocking = KNOCK_KNOCKING;
  }  
  else if (state_knocking == KNOCK_STORED)
  {
    state_knocking = KNOCK_IDLE;
    writeBlack();
    //timerStart(timer_knockDebounce);
  }    
    
  if (((state_knocking == KNOCK_SUCCESS) || (state_knocking == KNOCK_FAILED)) &&
      (timerFinished(timer_feedbackMode,time_feedbackMode)))
  {
    
    state_knocking = KNOCK_IDLE;
    writeBlack();
    //timer_knockDebounce = millis()+150; //Workaraound to prevent piezo from detecting false knocks
    //timerStart(timer_knockDebounce);
  }    


  //Clear events
  state_programSwitchChanged = false;
  state_knocked = false;
} 

void timerStart(unsigned long &timer) {
  timer = millis();
}  

boolean timerFinished(unsigned long &timer,unsigned long delaylength) {
  unsigned long now = millis();
  if ((now - timer) >= delaylength) {
    return true; 
  } else return false; 
}  

void updateLEDFrame() {
  //static unsigned long last_update = millis();  
  //if (((millis()-last_update) >= (1000 / LEDS_UPDATES_PER_SECOND)) && state_ledsUpdated) {
  if (state_ledsUpdated) {
    FastLED.show();
    state_ledsUpdated = false;
    //last_update = millis();
 }    
}

void moveLEDs(boolean back = false,boolean around = false) {
  if (back) {
    CRGB firstpixel = leds[0];
    for( int i = 0; i < (LEDS_NUM-1); i++) {
      leds[i] = leds[i+1];
    }
    if (around) leds[LEDS_NUM-1] = firstpixel;
    else leds[LEDS_NUM-1] = CRGB::Black; 
  }
  else  {
    CRGB lastpixel = leds[LEDS_NUM-1];
    for( int i = LEDS_NUM-1; i > 0; i--) {
      leds[i] = leds[i-1];
    }
    if (around) leds[0] = lastpixel;
    else leds[0] = CRGB::Black; 
  }  

  state_ledsUpdated = true;
}

void writePulse() {
  fill_gradient(leds,
    0,CHSV(60, 255,100),
    2,CHSV(0,255,255),SHORTEST_HUES);
  state_ledsUpdated = true;    
}  

void writeKeepAlive() {
  static CRGBPalette16 currentPalette = PartyColors_p;
  static unsigned long timer_lastMove = millis();

  static int led_positions[3] = {random(LEDS_NUM),random(LEDS_NUM),random(LEDS_NUM)};
  static byte led_colors[3] =   {random(256),random(256),random(256)};
  static unsigned int fade_position = 0;
  
  if (timerFinished(timer_lastMove,50)) {

    fade_position += 5;
    if (fade_position >= 255) {
      
      for (byte i=1;i<3;i++) {
        led_colors[i-1] = led_colors[i];
        led_positions[i-1] = led_positions[i];
      }
      
      fade_position = 0;
      
      led_colors[2] = random(256);
      led_positions[2] = random(0,LEDS_NUM);
      writeBlack();
    }
    
    //for (byte i=0;i<3;i++)
    leds[led_positions[0]] = ColorFromPalette(currentPalette , led_colors[0], 255-fade_position , BLEND);
    leds[led_positions[1]] = ColorFromPalette(currentPalette , led_colors[1], 255 , BLEND);
    leds[led_positions[2]] = ColorFromPalette(currentPalette , led_colors[2], fade_position , BLEND);      

    state_ledsUpdated = true;
    timerStart(timer_lastMove);    
  } 
} 

boolean writeMove() {
  static unsigned long timer_lastMove = millis();
  if (timerFinished(timer_lastMove,60)) {
    moveLEDs();
    timerStart(timer_lastMove);    
    return true;
  } else return false;
}  
  
void writeFail() {
  static unsigned long timer_lastMove = millis();
  static byte colorIndex = 0; 
  static CRGBPalette16 currentPalette = RainbowColors_p; //ForestColors_p;
  
  if (timerFinished(timer_lastMove,50)) {   
  
    
    for( byte i = 0; i < LEDS_NUM; i++) {
      //leds[i] = CHSV(colorIndex+(i),255,255);
      leds[i] = ColorFromPalette(currentPalette , colorIndex+(i*3), 255, BLEND);
    } 
    colorIndex +=4;
    
    //if (colorIndex > 64) colorIndex = 0;    
    state_ledsUpdated = true; 
    timerStart(timer_lastMove);
  }  
}  

void writeSuccess() {
  static unsigned long timer_lastMove = millis();
  static CRGB purple = CHSV( HUE_PURPLE, 255, 255);
  static CRGB green  = CHSV( HUE_GREEN, 255, 255);
  static CRGB black  = CRGB::Black;
  static CRGBPalette16 currentPalette = CRGBPalette16( 
    green,  green,  black,  black,
    purple, purple, black,  black,
    green,  green,  black,  black,
    purple, purple, black,  black ); 

  
  static int colorIndex = 0; 

  if (timerFinished(timer_lastMove,50)) {       
    for( int i = 0; i < LEDS_NUM; i++) {
      leds[i] = ColorFromPalette(currentPalette , colorIndex+(i*3), 255, BLEND);     
    } 
    colorIndex += 4;  

    state_ledsUpdated = true; 
    timerStart(timer_lastMove);
  }  
}  

void writeBlack() {
   fill_solid(leds, LEDS_NUM, CRGB::Black);
   state_ledsUpdated = true; 
}   


void moveServo(int position) {
  if (!state_servoAttached) {
    doorServo.attach(PIN_SERVO);
    state_servoAttached = true;
  }  
  doorServo.write(position);
  timerStart(timer_servoStop);
}

void stopServo() {
  if (state_servoAttached) {
    state_servoAttached = false;
    doorServo.detach();   
  }  
}



// Records the timing of knocks.
boolean recordKnockPattern(){
  static int currentKnockNumber=0;
  static int time_lastKnock=0; 
  
  //Begin knocking  
  if ((state_knocking == KNOCK_IDLE) && state_knocked) {
    int i = 0;
    for (i=0;i<maximumKnocks;i++){
      knockReadings[i]=0;
    }
    
    timerStart(timer_knockFinished);
    time_lastKnock = millis();    
    currentKnockNumber = 0;
    
    state_knocking = KNOCK_STARTED;    

  }
  
  //Continue knocking
  else if ((state_knocking == KNOCK_KNOCKING) &&  state_knocked) {
      int now=millis();
      knockReadings[currentKnockNumber] = now-time_lastKnock;
      currentKnockNumber ++;
      time_lastKnock=now;
      timerStart(timer_knockFinished);
  }

  //Stop knocking if timed out or enough knocks
  if (state_knocking == KNOCK_KNOCKING) {
    if (timerFinished(timer_knockFinished,time_knockFinished) || (currentKnockNumber > maximumKnocks)) {
        state_knocking = KNOCK_FINISHED;

        return true;
    }
  }  
  
  return false;  
}

void storeLock() {

  //Collect normalization data
  int maxKnockInterval = 0;                       
  for (int i=0;i<maximumKnocks;i++){  
    if (knockReadings[i] > maxKnockInterval){   
      maxKnockInterval = knockReadings[i];
    }
  }
  
  // Save pattern
  for (int i=0;i<maximumKnocks;i++){
    secretCode[i]= map(knockReadings[i],0, maxKnockInterval, 0, 100); 
    EEPROM.update(i,secretCode[i]);
  }

}

// Sees if our knock matches the secret.
// returns true if it's a good knock, false if it's not.

boolean validateKnock(){
  // simplest check first: Did we get the right number of knocks?
  int currentKnockCount = 0;
  int secretKnockCount = 0;
  int maxKnockInterval = 0;                     // We use this later to normalize the times.
  
  for (int i=0;i<maximumKnocks;i++){
    if (knockReadings[i] > 0){
      currentKnockCount++;
    }
    if (secretCode[i] > 0){                     //todo: precalculate this.
      secretKnockCount++;
    }
    
    if (knockReadings[i] > maxKnockInterval){   // collect normalization data while we're looping.
      maxKnockInterval = knockReadings[i];
    }
  }
  
  
  if (currentKnockCount != secretKnockCount){
    return false; 
  }
  
  /*  Now we compare the relative intervals of our knocks, not the absolute time between them.
      (ie: if you do the same pattern slow or fast it should still open the door.)
      This makes it less picky, which while making it less secure can also make it
      less of a pain to use if you're tempo is a little slow or fast. 
  */
  int totaltimeDifferences=0;
  int timeDiff=0;
  for (int i=0;i<maximumKnocks;i++){ // Normalize the times
    knockReadings[i]= map(knockReadings[i],0, maxKnockInterval, 0, 100);      
    timeDiff = abs(knockReadings[i]-secretCode[i]);
    if (timeDiff > knock_rejectValue){ // Individual value too far out of whack
      return false;
    }
    totaltimeDifferences += timeDiff;
  }
  // It can also fail if the whole thing is too inaccurate.
  if (totaltimeDifferences/secretKnockCount>knock_averageRejectValue){
    return false; 
  }
  
  return true;
  
}


