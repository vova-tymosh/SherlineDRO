#include "BluetoothSerial.h"


// --- PIN ASSIGNMENTS ---
#define ENCODER_X_A  16
#define ENCODER_X_B  17
#define ENCODER_Z_A  18
#define ENCODER_Z_B  19
#define ENCODER_TACHO  22


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

// Tacho sensor outputs 6 pulses per rotation
const int PULSES_PER_ROTATION = 6;
const int MAX_RPM = 12000; // Safety cap - lathe max around 10k RPM
volatile unsigned long tacho_pulse_count = 0; // Count pulses in current interval

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

// --- TIMER VARIABLES ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 40; // 40ms = ~25Hz refresh rate for TouchDRO
unsigned long lastRpmUpdateTime = 0;
const unsigned long rpmUpdateInterval = 500; // 500ms = 2Hz RPM update rate
int current_rpm = 0; // Last calculated RPM value

// --- INTERRUPT SERVICE ROUTINES (ISRs) ---
// High-speed, lightweight interrupt functions to track A/B quadrature transitions
void IRAM_ATTR isrX() {
  bool pinA = digitalRead(ENCODER_X_A);
  bool pinB = digitalRead(ENCODER_X_B);
  bool current_dir = (pinA == pinB);

  if (current_dir)
    raw_x_count++;
  else
    raw_x_count--;
  
  // if (current_dir != dir_x) {
  //   // Direction change detected! Record position and trigger backlash absorption
  //   dir_x = current_dir;
  //   last_x_at_dir_change = raw_x_count;
  //   backlash_active_x = true;
  // }
}

void IRAM_ATTR isrZ() {
  bool pinA = digitalRead(ENCODER_Z_A);
  bool pinB = digitalRead(ENCODER_Z_B);
  bool current_dir = (pinA == pinB);

  if (current_dir)
    raw_z_count++;
  else
    raw_z_count--;
  
  // if (current_dir != dir_z) {
  //   dir_z = current_dir;
  //   last_z_at_dir_change = raw_z_count;
  //   backlash_active_z = true;
  // }
}

void IRAM_ATTR isrTacho() {
  tacho_pulse_count++;
}

void setup() {
  Serial.begin(115200);
  
  SerialBT.begin("Sherline_DRO"); 
  Serial.println("Bluetooth DRO Controller: Sherline_DRO");
  
  pinMode(ENCODER_X_A, INPUT_PULLUP);
  pinMode(ENCODER_X_B, INPUT_PULLUP);
  pinMode(ENCODER_Z_A, INPUT_PULLUP);
  pinMode(ENCODER_Z_B, INPUT_PULLUP);
  pinMode(ENCODER_TACHO, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(ENCODER_X_A), isrX, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_Z_A), isrZ, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_TACHO), isrTacho, RISING);
}

void loop() {

  // Copy volatile variables (atomic on 32-bit ESP32)
  long snap_raw_x = raw_x_count;
  long snap_raw_z = raw_z_count;
  
  // Update RPM calculation at 2Hz (every 500ms)
  if (millis() - lastRpmUpdateTime >= rpmUpdateInterval) {
    // Read and reset pulse counter atomically using exchange
    unsigned long pulse_count = tacho_pulse_count;
    tacho_pulse_count = 0;
    
    // Calculate RPM from pulse count over time interval
    // pulses_per_second = pulse_count / (interval_ms / 1000)
    // rotations_per_second = pulses_per_second / PULSES_PER_ROTATION
    // RPM = rotations_per_second * 60
    // Simplified: RPM = (pulse_count * 60 * 1000) / (interval_ms * PULSES_PER_ROTATION)
    unsigned long calculated_rpm = (pulse_count * 60 * 1000) / (rpmUpdateInterval * PULSES_PER_ROTATION);
    
    // Apply sanity check - cap at maximum expected RPM
    if (calculated_rpm <= MAX_RPM) {
      current_rpm = calculated_rpm;
    } else {
      current_rpm = 0; // Invalid reading, probably noise
    }
    
    lastRpmUpdateTime = millis();
  }
  
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

  // Stream formatted data block to TouchDRO over Bluetooth at 25 Hz
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    
    SerialBT.print("x");SerialBT.print(snap_raw_x);SerialBT.println(";");
    SerialBT.print("z");SerialBT.print(snap_raw_z);SerialBT.println(";");
    SerialBT.print("t");SerialBT.print(current_rpm);SerialBT.println(";");
    
    Serial.print("x");Serial.print(snap_raw_x);Serial.println(";");
    Serial.print("z");Serial.print(snap_raw_z);Serial.println(";");
    Serial.print("t");Serial.print(current_rpm);Serial.println(";");
  }
}

