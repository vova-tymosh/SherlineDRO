#include "BluetoothSerial.h"


// --- ENCODER AXIS CLASS ---
class EncoderAxis {
public:
  int pinA;
  int pinB;
  int backlashPulses;
  volatile bool dir = true;
  volatile int backlash_counter = 0;
  volatile long count = 0;
  
  EncoderAxis(int pinA, int pinB, int backlashPulses) 
    : pinA(pinA), pinB(pinB), backlashPulses(backlashPulses) {}
  
  void begin() {
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
  }
  
  long getCount() const { return count; }
};

// --- PIN ASSIGNMENTS ---
#define ENCODER_X_A  16
#define ENCODER_X_B  17
#define ENCODER_Z_A  18
#define ENCODER_Z_B  19
#define ENCODER_TACHO  22

// --- BACKLASH SETTINGS ---
// Set these to the exact number of physical pulses of "slop" your handwheels have.
// To disable backlash compensation, set these to 0.
const int BACKLASH_X_PULSES = 1; 
const int BACKLASH_Z_PULSES = 1;

// --- ENCODER INSTANCES ---
EncoderAxis axisX(ENCODER_X_A, ENCODER_X_B, BACKLASH_X_PULSES);
EncoderAxis axisZ(ENCODER_Z_A, ENCODER_Z_B, BACKLASH_Z_PULSES);

BluetoothSerial SerialBT;

// Tacho sensor outputs 6 pulses per rotation
const int PULSES_PER_ROTATION = 6;
const int MAX_RPM = 12000; // Safety cap - lathe max around 10k RPM
volatile unsigned long tacho_pulse_count = 0; // Count pulses in current interval

// --- TIMER VARIABLES ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 40; // 40ms = ~25Hz refresh rate for TouchDRO
unsigned long lastRpmUpdateTime = 0;
const unsigned long rpmUpdateInterval = 500; // 500ms = 2Hz RPM update rate
int current_rpm = 0; // Last calculated RPM value

// --- INTERRUPT SERVICE ROUTINES (ISRs) ---
void IRAM_ATTR isrX() {
  bool pinA = digitalRead(axisX.pinA);
  bool pinB = digitalRead(axisX.pinB);
  bool current_dir = (pinA == pinB);
  
  // Check for direction change
  if (current_dir != axisX.dir) {
    axisX.dir = current_dir;
    axisX.backlash_counter = 0; // Reset backlash counter on direction change
  } else if (axisX.backlash_counter < axisX.backlashPulses) {
    // Still absorbing backlash, don't update output
    axisX.backlash_counter++;
  } else {
    // Backlash absorbed, update output counter
    if (current_dir)
      axisX.count++;
    else
      axisX.count--;
  }
}

void IRAM_ATTR isrZ() {
  bool pinA = digitalRead(axisZ.pinA);
  bool pinB = digitalRead(axisZ.pinB);
  bool current_dir = (pinA == pinB);
  
  // Check for direction change
  if (current_dir != axisZ.dir) {
    axisZ.dir = current_dir;
    axisZ.backlash_counter = 0; // Reset backlash counter on direction change
  } else if (axisZ.backlash_counter < axisZ.backlashPulses) {
    // Still absorbing backlash, don't update output
    axisZ.backlash_counter++;
  } else {
    // Backlash absorbed, update output counter
    if (current_dir)
      axisZ.count++;
    else
      axisZ.count--;
  }
}

void IRAM_ATTR isrTacho() {
  tacho_pulse_count++;
}

void setup() {
  Serial.begin(115200);
  
  SerialBT.begin("Sherline_DRO"); 
  Serial.println("Bluetooth DRO Controller: Sherline_DRO");
  
  axisX.begin();
  axisZ.begin();
  pinMode(ENCODER_TACHO, INPUT_PULLUP);
  
  attachInterrupt(digitalPinToInterrupt(ENCODER_X_A), isrX, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_Z_A), isrZ, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENCODER_TACHO), isrTacho, RISING);
}

void loop() {

  // Get encoder counts (atomic on 32-bit ESP32)
  long snap_out_x = axisX.getCount();
  long snap_out_z = axisZ.getCount();
  
  // Update RPM calculation at 2Hz (every 500ms)
  if (millis() - lastRpmUpdateTime >= rpmUpdateInterval) {
    // Read and reset pulse counter
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

  // Stream formatted data block to TouchDRO over Bluetooth at 25 Hz
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    
    SerialBT.print("x");SerialBT.print(snap_out_x);SerialBT.println(";");
    SerialBT.print("z");SerialBT.print(snap_out_z);SerialBT.println(";");
    SerialBT.print("t");SerialBT.print(current_rpm);SerialBT.println(";");
    
    Serial.print("x");Serial.print(snap_out_x);Serial.println(";");
    Serial.print("z");Serial.print(snap_out_z);Serial.println(";");
    Serial.print("t");Serial.print(current_rpm);Serial.println(";");
  }
}
