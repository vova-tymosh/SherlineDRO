#include "BluetoothSerial.h"


// --- PIN ASSIGNMENTS ---
// Adjust these to match your physical ESP32 wiring
#define ENCODER_X_A  16
#define ENCODER_X_B  17

#define ENCODER_Z_A  18
#define ENCODER_Z_B  19

#define ENCODER_TACHO  22


// --- BLUETOOTH CONFIG ---
BluetoothSerial SerialBT;

// --- BACKLASH SETTINGS (Modify these based on your physical measurements!) ---
// Change these to the exact number of physical pulses of "slop" your handwheels have.
// To disable backlash compensation, set these to 0.
// const int BACKLASH_X_PULSES = 1; 
// const int BACKLASH_Z_PULSES = 1;

// --- VOLATILE ENCODER STATE ---
// Must be volatile because they are modified inside Interrupt Service Routines (ISRs)
volatile long raw_x_count = 0;
volatile long raw_z_count = 0;
volatile long raw_tacho_count = 0;

// Debounce timing - 250 interrupts/sec = 4ms minimum between interrupts (4000 microseconds)
volatile unsigned long last_x_micros = 0;
volatile unsigned long last_z_micros = 0;
volatile unsigned long last_tacho_micros = 0;
const unsigned long DEBOUNCE_MICROS = 4000; // 4ms = 250 Hz max

// Track the physical direction of rotation (true = forward, false = backward)
// volatile bool dir_x = true;
// volatile bool dir_z = true;

// --- OUTPUT COUNTER VARIABLES (Passed to TouchDRO) ---
// long out_x_count = 0;
// long out_z_count = 0;

// Variables used to manage backlash transitions
// long last_x_at_dir_change = 0;
// long last_z_at_dir_change = 0;
// bool backlash_active_x = false;
// bool backlash_active_z = false;

// --- TIMER VARIABLE ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 40; // 40ms = ~25Hz refresh rate

// --- INTERRUPT SERVICE ROUTINES (ISRs) ---
// High-speed, lightweight interrupt functions to track A/B quadrature transitions
void IRAM_ATTR isrX() {

  bool pinA = digitalRead(ENCODER_X_A);
  bool pinB = digitalRead(ENCODER_X_B);
  bool current_dir = (pinA == pinB);

  unsigned long current_micros = micros();
  if (current_micros - last_x_micros >= DEBOUNCE_MICROS) {
    if (current_dir)
      raw_x_count++;
    else
      raw_x_count--;
  
    last_x_micros = current_micros;
  }
  
  // if (current_dir != dir_x) {
  //   // Direction change detected! Record position and trigger backlash absorption
  //   dir_x = current_dir;
  //   last_x_at_dir_change = raw_x_count;
  //   backlash_active_x = true;
  // }
}

void IRAM_ATTR isrZ() {
  unsigned long current_micros = micros();
  if (current_micros - last_z_micros >= DEBOUNCE_MICROS) {
    bool pinA = digitalRead(ENCODER_Z_A);
    bool pinB = digitalRead(ENCODER_Z_B);
    bool current_dir = (pinA == pinB);
  
    if (current_dir)
      raw_z_count++;
    else
      raw_z_count--;

    last_z_micros = current_micros;
  }

  
  // if (current_dir != dir_z) {
  //   dir_z = current_dir;
  //   last_z_at_dir_change = raw_z_count;
  //   backlash_active_z = true;
  // }
}

void IRAM_ATTR isrTacho() {
  unsigned long current_micros = micros();
  if (current_micros - last_tacho_micros >= DEBOUNCE_MICROS) {
    raw_tacho_count++;
    last_tacho_micros = current_micros;
  }
}

void setup() {
  Serial.begin(115200);
  
  // Initialize Bluetooth Classic with the broadcast name TouchDRO looks for
  SerialBT.begin("Sherline_DRO"); 
  Serial.println("Bluetooth DRO Controller: Sherline_DRO");
  
  // Set pins as INPUT with internal pull-ups (highly recommended for optical sensors)
  pinMode(ENCODER_X_A, INPUT_PULLUP);
  pinMode(ENCODER_X_B, INPUT_PULLUP);
  pinMode(ENCODER_Z_A, INPUT_PULLUP);
  pinMode(ENCODER_Z_B, INPUT_PULLUP);
  pinMode(ENCODER_TACHO, INPUT_PULLUP);
  
  // Attach interrupts to trigger on both rising and falling edges of Channel A
  attachInterrupt(digitalPinToInterrupt(ENCODER_X_A), isrX, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_Z_A), isrZ, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_TACHO), isrTacho, RISING);
}

void loop() {

  // 1. Process Backlash Logic outside of ISR to keep interrupts blazing fast
  // noInterrupts(); // Temporarily pause interrupts to safely copy volatile variables
  long snap_raw_x = raw_x_count;
  long snap_raw_z = raw_z_count;
  long snap_raw_tacho = raw_tacho_count;
  // interrupts();   // Resume interrupts
  
  // --- X-AXIS BACKLASH FILTER ---
  // if (backlash_active_x) {
  //   long distance_since_turn = abs(snap_raw_x - last_x_at_dir_change);
  //   if (distance_since_turn >= BACKLASH_X_PULSES) {
  //     // Slop has been taken up! Resume tracking relative to where movement restarted
  //     backlash_active_x = false;
  //     if (dir_x) {
  //       out_x_count = out_x_count + (distance_since_turn - BACKLASH_X_PULSES);
  //     } else {
  //       out_x_count = out_x_count - (distance_since_turn - BACKLASH_X_PULSES);
  //     }
  //   }
  //   // While active, we purposely do not change out_x_count (the screen freezes)
  // } else {
  //   // Standard linear tracking mode
  //   static long prev_raw_x = snap_raw_x;
  //   out_x_count += (snap_raw_x - prev_raw_x);
  //   prev_raw_x = snap_raw_x;
  // }

  // --- Z-AXIS BACKLASH FILTER ---
  // if (backlash_active_z) {
  //   long distance_since_turn = abs(snap_raw_z - last_z_at_dir_change);
  //   if (distance_since_turn >= BACKLASH_Z_PULSES) {
  //     backlash_active_z = false;
  //     if (dir_z) {
  //       out_z_count = out_z_count + (distance_since_turn - BACKLASH_Z_PULSES);
  //     } else {
  //       out_z_count = out_z_count - (distance_since_turn - BACKLASH_Z_PULSES);
  //     }
  //   }
  // } else {
  //   static long prev_raw_z = snap_raw_z;
  //   out_z_count += (snap_raw_z - prev_raw_z);
  //   prev_raw_z = snap_raw_z;
  // }

  // 2. Stream formatted data block to TouchDRO over Bluetooth at 25 Hz
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    
    SerialBT.print("x");SerialBT.print(snap_raw_x);SerialBT.println(";");
    SerialBT.print("z");SerialBT.print(snap_raw_z);SerialBT.println(";");
    SerialBT.print("t");SerialBT.print(snap_raw_tacho);SerialBT.println(";");
    
    Serial.print("x");Serial.print(snap_raw_x);Serial.println(";");
    Serial.print("z");Serial.print(snap_raw_z);Serial.println(";");
    Serial.print("t");Serial.print(snap_raw_tacho);Serial.println(";");
}




}

