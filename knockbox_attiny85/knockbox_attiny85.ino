
/* Detects patterns of knocks and triggers a servo to unlock
   a door if the pattern is correct. */   
   
/* Validating a knock is based on:

   By Steve Hoefer http://grathio.com
   Version 0.1.10.20.10
   Licensed under Creative Commons Attribution-Noncommercial-Share Alike 3.0
   http://creativecommons.org/licenses/by-nc-sa/3.0/us/
   (In short: Do what you want, just be sure to include this line and the four above it, and don't sell it or use it in anything you sell without contacting me.)
*/

/*
   WIRING
  
   Analog Pin 1: Piezo speaker (connected to ground with 1M pulldown resistor)
   Digital Pin 3: Switch to enter a new code.  Short this to enter programming mode.
   Digital Pin 4: Servo
   Digital Pin 0: Red LED. 
   Digital Pin 1: Green LED. 
   
*/
 
//Servo
#include <SoftwareServo.h>
#define PIN_SERVO 4           // Gear motor used to turn the lock.
SoftwareServo doorServo;

//Memory
#include <EEPROM.h>

//Light
#define PIN_LED_RED 0
#define PIN_LED_BLUE 1


// Sensors
#define PIN_SENSOR_KNOCK A1  // Piezo sensor on pin 0.
#define PIN_SENSOR_SWITCH 3  // Switch to program new code

 
// Tuning constants.
const int knock_threshold = 20;           // Minimum signal from the piezo to register as a knock
const int knock_rejectValue = 25;        // If an individual knock is off by this percentage of a knock we don't unlock..
const int knock_averageRejectValue = 15; // If the average timing of the knocks is off by this percent we don't unlock.

// Knock pattern
const int maximumKnocks = 20;       // Maximum number of knocks to listen for.
byte secretCode[maximumKnocks] = {50, 25, 25, 50, 100, 50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // Initial setup: "Shave and a Hair Cut, two bits."
int knockReadings[maximumKnocks];   // When someone knocks this array fills with delays between knocks.
int value_knockSensor = 0;           // Last reading of the knock sensor.


//States
#define KNOCK_NONE 0
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
int state_knocking = KNOCK_NONE;
boolean state_knocked = false;

//Timers
unsigned long timer_knockDebounce = 0;
unsigned long timer_knockFinished = 0;
unsigned long timer_servoStop = 0;
unsigned long timer_ledKnock = 0;
unsigned long timer_ledFail = 0;
unsigned long timer_ledSuccess = 0;
unsigned long timer_blockMode = 0;

const unsigned long time_knockDebounce = 150;     // milliseconds we allow a knock to fade before we listen for another one. (Debounce timer.)
const unsigned long time_servoStop = 1000;      // milliseconds that we run the motor to get it to go a half turn.
const unsigned long time_knockFinished = 1700;     // Longest time to wait for a knock before we assume that it's finished.
const unsigned long time_ledKnock = 150;   
const unsigned long time_blockMode = 1000;   

void setup() {     
  delay(100); 
   
  //Configure pins  
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_BLUE, OUTPUT);
  pinMode(PIN_SENSOR_SWITCH, INPUT);
  digitalWrite(PIN_SENSOR_SWITCH, HIGH);      
   
  //Load pattern
  for (int i=0;i<maximumKnocks;i++){
    secretCode[i]= EEPROM.read(i);
  }
   
  //Position servo
  digitalWrite(PIN_LED_BLUE, HIGH);   
  digitalWrite(PIN_LED_RED, HIGH);
  delay(time_blockMode);
  
  stopServo();    
  writeBlack();  
}

void loop() {
  
  //Check program button
  if (digitalRead(PIN_SENSOR_SWITCH) != state_programSwitch) {      
      state_programSwitch = !state_programSwitch;
      state_programSwitchChanged = true;
      if (!state_programSwitch) state_programming = true; //pullup -> pulldown
  } 
  
  // Listen for knocks
  if (timerFinished(timer_knockDebounce,time_knockDebounce)) {
    value_knockSensor = analogRead(PIN_SENSOR_KNOCK);
    state_knocked = (value_knockSensor > knock_threshold);
    if (state_knocked) {
       timerStart(timer_knockDebounce);
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
    //moveServo(0);
    //state_doorOpen=false;
    //timerStart(timer_blockMode);
    
  }

  else if (state_knocking == KNOCK_SUCCESS_STARTED) {
    state_doorOpen = !state_doorOpen; 
    moveServo(map(state_doorOpen,0,1,0,180));  
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
    //fill_solid(leds, LEDS_NUM, CRGB::Black);
  }
  
  if  (((state_knocking == KNOCK_STARTED) || (state_knocking == KNOCK_KNOCKING) )  && (state_knocked)) {
    //addPulse();
    digitalWrite(PIN_LED_BLUE, HIGH);
    timerStart(timer_ledKnock);
  }
  
  if (timerFinished(timer_ledKnock,time_ledKnock) && (state_knocking != KNOCK_SUCCESS))
    digitalWrite(PIN_LED_BLUE, LOW);          


  //Advance states
  if (state_knocking == KNOCK_SUCCESS_STARTED)
  {
    state_knocking = KNOCK_SUCCESS;
    timerStart(timer_blockMode);
    writeSuccess();
  }  
  else if (state_knocking == KNOCK_FAILED_STARTED)
  {
    state_knocking = KNOCK_FAILED;
    timerStart(timer_blockMode);
    writeFail();
  } 
  
  else if (state_knocking == KNOCK_STARTED)
  {
    state_knocking = KNOCK_KNOCKING;
  }  
  else if (state_knocking == KNOCK_STORED)
  {
    state_knocking = KNOCK_SUCCESS;
    //writeBlack();
  }    
    
  if (((state_knocking == KNOCK_SUCCESS) || (state_knocking == KNOCK_FAILED)) &&
      (timerFinished(timer_blockMode,time_blockMode)))
  {
    state_knocking = KNOCK_NONE;
    stopServo(); 
    writeBlack();
  }    
  
  //Clear events
  state_programSwitchChanged = false;
  state_knocked = false;

  if (state_servoAttached)
    SoftwareServo::refresh();
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

void writeFail() {
  digitalWrite(PIN_LED_RED, HIGH);
}  

void writeSuccess() {
  digitalWrite(PIN_LED_BLUE, HIGH);
}  

void writeBlack() {
    digitalWrite(PIN_LED_RED, LOW);  
    digitalWrite(PIN_LED_BLUE, LOW); 
}   

// Records the timing of knocks.
boolean recordKnockPattern(){
  static int currentKnockNumber=0;
  static int time_lastKnock=0; 
  
  //Begin knocking  
  if ((state_knocking == KNOCK_NONE) && state_knocked) {
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

