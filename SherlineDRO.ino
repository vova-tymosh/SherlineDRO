#include "BluetoothSerial.h"
#include "esp_bt_device.h"

// --- PIN ASSIGNMENTS ---
#define ENCODER_X_A  16
#define ENCODER_X_B  17
#define ENCODER_Y_A  21
#define ENCODER_Y_B  26
#define ENCODER_Z_A  18
#define ENCODER_Z_B  19
#define ENCODER_TACHO  22

// --- BACKLASH SETTINGS ---
// Set these to the exact number of physical pulses of "slop" your handwheels have.
// To disable backlash compensation, set these to 0.
const int BACKLASH_X_PULSES = 2;
const int BACKLASH_Y_PULSES = 1;
const int BACKLASH_Z_PULSES = 5;

// --- TACHO SETTING ---
const int PULSES_PER_ROTATION = 6;
const int MAX_RPM = 12000; // Safety cap - lathe max around 10k RPM



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
  
  void begin(void (*isr)()) {
    pinMode(pinA, INPUT_PULLUP);
    pinMode(pinB, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pinA), isr, CHANGE);
  }
  
  // Process encoder pulse - called from ISR
  inline void processPulse(bool current_dir) {
    // Check for direction change
    if (current_dir != dir) {
      dir = current_dir;
      backlash_counter = 0; // Reset backlash counter on direction change
    }
    
    // Check if we're still absorbing backlash
    if (backlash_counter < backlashPulses) {
      // Still absorbing backlash, don't update output
      backlash_counter++;
    } else {
      // Backlash absorbed, update output counter
      if (current_dir)
        count++;
      else
        count--;
    }
  }
  
  long getCount() const { return count; }
};

// --- TACHO SENSOR CLASS ---
class TachoSensor {
public:
  int pin;
  int pulsesPerRotation;
  int maxRPM;
  volatile unsigned long pulseCount = 0;
  
  TachoSensor(int pin, int pulsesPerRotation, int maxRPM)
    : pin(pin), pulsesPerRotation(pulsesPerRotation), maxRPM(maxRPM) {}
  
  void begin(void (*isr)()) {
    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), isr, RISING);
  }
  
  inline void handlePulse() {
    pulseCount++;
  }
  
  int calculateRPM(unsigned long intervalMs) {
    // Read and reset pulse counter
    unsigned long count = pulseCount;
    pulseCount = 0;
    
    // Calculate RPM: (pulses * 60 * 1000) / (interval_ms * pulses_per_rotation)
    unsigned long calculated_rpm = (count * 60 * 1000) / (intervalMs * pulsesPerRotation);
    
    // Apply sanity check - cap at maximum expected RPM
    if (calculated_rpm <= maxRPM) {
      return calculated_rpm;
    } else {
      return 0; // Invalid reading, probably noise
    }
  }
};

// --- ENCODER INSTANCES ---
EncoderAxis axisX(ENCODER_X_A, ENCODER_X_B, BACKLASH_X_PULSES);
EncoderAxis axisY(ENCODER_Y_A, ENCODER_Y_B, BACKLASH_Y_PULSES);
EncoderAxis axisZ(ENCODER_Z_A, ENCODER_Z_B, BACKLASH_Z_PULSES);

// --- TACHO SENSOR ---
TachoSensor tacho(ENCODER_TACHO, PULSES_PER_ROTATION, MAX_RPM);

BluetoothSerial SerialBT;

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
  axisX.processPulse(current_dir);
}

void IRAM_ATTR isrY() {
  bool pinA = digitalRead(axisY.pinA);
  bool pinB = digitalRead(axisY.pinB);
  bool current_dir = (pinA == pinB);
  axisY.processPulse(current_dir);
}

void IRAM_ATTR isrZ() {
  bool pinA = digitalRead(axisZ.pinA);
  bool pinB = digitalRead(axisZ.pinB);
  bool current_dir = (pinA == pinB);
  axisZ.processPulse(current_dir);
}

void IRAM_ATTR isrTacho() {
  tacho.handlePulse();
}

void setup() {
  Serial.begin(115200);
  
  SerialBT.begin("Sherline_DRO");

  axisX.begin(isrX);
  axisY.begin(isrY);
  axisZ.begin(isrZ);
  tacho.begin(isrTacho);
}

void loop() {

  // Get encoder counts (atomic on 32-bit ESP32)
  long snap_out_x = axisX.getCount();
  long snap_out_y = axisY.getCount();
  long snap_out_z = axisZ.getCount();
  
  
  // Update RPM calculation at 2Hz (every 500ms)
  if (millis() - lastRpmUpdateTime >= rpmUpdateInterval) {
    current_rpm = tacho.calculateRPM(rpmUpdateInterval);
    lastRpmUpdateTime = millis();
  }

  // Stream formatted data block to TouchDRO over Bluetooth at 25 Hz
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    
    SerialBT.print("x");SerialBT.print(snap_out_x);SerialBT.println(";");
    SerialBT.print("y");SerialBT.print(snap_out_y);SerialBT.println(";");
    SerialBT.print("z");SerialBT.print(snap_out_z);SerialBT.println(";");
    SerialBT.print("t");SerialBT.print(current_rpm);SerialBT.println(";");
    
    //Uncomment the following if you want to see the output in Serial Monitor
    // Serial.print("x");Serial.print(snap_out_x);Serial.println(";");
    // Serial.print("y");Serial.print(snap_out_y);Serial.println(";");
    // Serial.print("z");Serial.print(snap_out_z);Serial.println(";");
    // Serial.print("t");Serial.print(current_rpm);Serial.println(";");
  }
}
