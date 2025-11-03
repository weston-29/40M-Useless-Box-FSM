/*
  ENGR 40M Lab 2b
  Weston Keller
  Fall 2025

  This version of the useless box FSM implements two special states - Angry and Threat.
  Both states occur after 5 presses of the DPDT have occurred within 5s, and alternate their activation

  Angry - finger thrashes back and forth before pressing the switch

  Threat - finger waits for 4s on the limit switch, comes up ALMOST to the switch and pauses before 
  definitively pressing the DPDT and slinking back into resting position. User input is ignored during this sequence.
*/

// Abstractions for Switch Depression
#define ON  LOW   // because INPUT_PULLUP: pressed/connected = LOW
#define OFF HIGH  // open/disconnected = HIGH

// Pin Definitions
const int MOTOR1   = 6;   // Motor input 1 - Red Wire
const int MOTOR2   = 7;   // Motor input 2 - Black Wire
const int SPDT_PIN = A1;  // SPDT toggle switch (other side to GND)
const int DPDT_PIN = A0;  // DPDT limit switch (other side to GND)

// Escalation parameters
const int ESCALATION_THRESHOLD = 5;    // flips to trigger escalation
const int ESCALATION_TIMEFRAME = 5000; // 5 seconds

// Angry mode parameters
const int ANGRY_HAMMERS = 5;            // number of angry hammer hits
const int PARTIAL_RETRACT_MS = 200;     // brief retract between hammers

// Threat mode parameters
const int THREAT_HOLD_MS = 4000;        // hold on switch for 4 seconds
const int THREAT_RETRACT_MS = 400;      // time to retract halfway
const int THREAT_PAUSE_MS = 500;        // pause at halfway point
const int THREAT_JERK_MS = 150;         // quick jerk forward

enum states { STOP = 0, FORWARD, REVERSE, ANGRY, THREAT };
int state = STOP; // Start with motor off

// Flip tracking
unsigned long flipTimes[ESCALATION_THRESHOLD];
int flipIndex = 0;
bool nextEscalationIsAngry = true; // alternates between angry and threat

// State-specific tracking
int angryHammerCount = 0;
unsigned long stateStartTime = 0;
int threatPhase = 0; // 0=press, 1=hold, 2=retract, 3=pause, 4=jerk, 5=return

// Edge detection for DPDT
int lastDpdtState = OFF;


void motorStop() {
  digitalWrite(MOTOR1, LOW);
  digitalWrite(MOTOR2, LOW);
}


void motorForward() {
  digitalWrite(MOTOR1, LOW);
  digitalWrite(MOTOR2, HIGH);
}


void motorReverse() {
  digitalWrite(MOTOR1, HIGH);
  digitalWrite(MOTOR2, LOW);
}


void setup() {
  // serial monitor Baud rate
  Serial.begin(115200);
  Serial.println("Setup complete");

  // configure pin I/O
  pinMode(MOTOR1, OUTPUT);
  pinMode(MOTOR2, OUTPUT);
  pinMode(SPDT_PIN, INPUT_PULLUP);
  pinMode(DPDT_PIN, INPUT_PULLUP);

  // Initialize motor to be off when we start
  motorStop();

  // Initialize flip tracking
  for (int i = 0; i < ESCALATION_THRESHOLD; i++) {
    flipTimes[i] = 0;
  }
}


bool checkEscalation() {
  // record this flip
  flipTimes[flipIndex] = millis();
  flipIndex = (flipIndex + 1) % ESCALATION_THRESHOLD;

  // check if last 5 flips happened within timeframe
  unsigned long oldestFlip = flipTimes[flipIndex];
  unsigned long newestFlip = millis();
  
  if (oldestFlip > 0 && (newestFlip - oldestFlip) < ESCALATION_TIMEFRAME) {
    if (nextEscalationIsAngry) {
      Serial.println("ANGRY MODE TRIGGERED!");
    } else {
      Serial.println("THREAT MODE TRIGGERED!");
    }
    return true;
  }
  return false;
}


void resetFlipCounter() {
  for (int i = 0; i < ESCALATION_THRESHOLD; i++) {
    flipTimes[i] = 0;
  }
  flipIndex = 0;
  Serial.println("Flip counter reset");
}


void loop() {
  // read switch states
  int spdtState = digitalRead(SPDT_PIN);
  int dpdtState = digitalRead(DPDT_PIN);

  // detect rising edge on DPDT (user flipped switch ON)
  bool userFlipped = (dpdtState == ON && lastDpdtState == OFF);
  lastDpdtState = dpdtState;

  // debug switches in serial monitor
  Serial.print("SPDT: ");
  Serial.print(spdtState == ON ? "ON  " : "OFF  ");

  Serial.print("DPDT: ");
  Serial.print(dpdtState == ON ? "ON  " : "OFF  ");

  Serial.println();

  // FSM Core
  switch (state) {
    case STOP:
      Serial.println("State: STOP");
      motorStop();

      // Transitions
      if (spdtState == ON) {
        state = REVERSE; 
      }
      if (userFlipped) {
        if (checkEscalation()) {
          if (nextEscalationIsAngry) {
            angryHammerCount = 0;
            state = ANGRY;
            nextEscalationIsAngry = false; // next time will be threat
          } else {
            threatPhase = 0;
            stateStartTime = millis();
            state = THREAT;
            nextEscalationIsAngry = true; // next time will be angry
          }
        } else {
          state = FORWARD;
        }
      }
         
      break;

    case FORWARD:
      Serial.println("State: FORWARD (towards switch)");
      motorForward();

      if (dpdtState == ON) {
        state = FORWARD; // user pressed the switch again 
      } else { 
        state = REVERSE; // go back to home
      } 

      break;


    case REVERSE:
      Serial.println("State: REVERSE (away from switch)");
      motorReverse();

      // Transitions
      if (spdtState == ON) { 
        state = STOP; // limit switch depressed 
      }
      if (userFlipped) {
        if (checkEscalation()) {
          if (nextEscalationIsAngry) {
            angryHammerCount = 0;
            state = ANGRY;
            nextEscalationIsAngry = false; // next time will be threat
          } else {
            threatPhase = 0;
            stateStartTime = millis();
            state = THREAT;
            nextEscalationIsAngry = true; // next time will be angry
          }
        } else {
          state = FORWARD;
        }
      }
      break;

    case ANGRY:
      Serial.print("State: ANGRY (hammer ");
      Serial.print(angryHammerCount + 1);
      Serial.print("/");
      Serial.print(ANGRY_HAMMERS);
      Serial.println(")");

      // hammer forward to hit switch
      motorForward();
      delay(300); // push forward until we hit it

      // brief partial retract
      motorReverse();
      delay(PARTIAL_RETRACT_MS);

      angryHammerCount++;

      // done hammering?
      if (angryHammerCount >= ANGRY_HAMMERS) {
        resetFlipCounter(); // reset after angry finishes
        state = REVERSE; // go all the way home
      }

      break;

    case THREAT:
      {
        unsigned long elapsed = millis() - stateStartTime;

        switch (threatPhase) {
          case 0: // Come up just before switch
            Serial.println("State: THREAT - Pressing switch");
            motorForward();
            
            if (dpdtState == OFF) { // successfully pressed it
              threatPhase = 1;
              stateStartTime = millis();
              motorStop();
            }
            break;

          case 1: // Hold on switch (menacing muahahaha)
            Serial.print("State: THREAT - Holding (");
            Serial.print(elapsed);
            Serial.println(" ms)");
            motorStop();
            
            if (elapsed >= THREAT_HOLD_MS) {
              threatPhase = 2;
              stateStartTime = millis();
            }
            break;

          case 2: // Retract slightly
            Serial.println("State: THREAT - Retracting slightly");
            motorReverse();
            
            if (elapsed >= THREAT_RETRACT_MS) {
              threatPhase = 3;
              stateStartTime = millis();
              motorStop();
            }
            break;

          case 3: // Pause
            Serial.println("State: THREAT - Pausing");
            motorStop();
            
            if (elapsed >= THREAT_PAUSE_MS) {
              threatPhase = 4;
              stateStartTime = millis();
            }
            break;

          case 4: // Quick jerk forward to snarkily press switch
            Serial.println("State: THREAT - Jerk!");
            motorForward();
            
            if (elapsed >= THREAT_JERK_MS) {
              threatPhase = 5;
              motorStop();
            }
            break;

          case 5: // Return home and finish
            Serial.println("State: THREAT - Returning home");
            motorReverse();
            
            if (spdtState == ON) { // reached home
              resetFlipCounter(); // reset after threat finishes
              state = STOP;
              threatPhase = 0; // reset for next time
            }
            break;
        }
      }
      break;

    default:
      Serial.println("Invalid state!");
      motorStop();
      state = STOP;
      break;
  }

  delay(100); // brief debounce / serial readability - thanks 107E evil switch project for the tip
}