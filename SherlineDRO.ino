#include "BluetoothSerial.h"
#include "esp_bt_device.h"
#include "Tacho.h"
#include "driver/pcnt.h"

// --- PIN ASSIGNMENTS ---
#define ENCODER_X_A  16
#define ENCODER_X_B  17
#define ENCODER_Y_A  21
#define ENCODER_Y_B  26
#define ENCODER_Z_A  18
#define ENCODER_Z_B  19
#define TACHO_RX_PIN  5

// --- BACKLASH SETTINGS ---
// Set these to the exact number of physical pulses of "slop" your handwheels have.
// To disable backlash compensation, set these to 0.
const int BACKLASH_X_PULSES = 3;
const int BACKLASH_Y_PULSES = 1;
const int BACKLASH_Z_PULSES = 5;

// --- DIRECTION SETTINGS ---
// Flip these if an axis counts the wrong way for the physical layout of the lathe.
const bool INVERT_X = true;
const bool INVERT_Y = true;
const bool INVERT_Z = true;



// --- ENCODER AXIS CLASS ---
class EncoderAxis {
public:
  int pinA;
  int pinB;
  int backlashPulses;
  pcnt_unit_t pcntUnit;
  bool invert;
  volatile bool dir = true;
  volatile int backlash_counter = 0;
  int16_t lastCount = 0;
  volatile long count = 0;
  
  EncoderAxis(int pinA, int pinB, int backlashPulses, pcnt_unit_t unit, bool invert = false) 
    : pinA(pinA), pinB(pinB), backlashPulses(backlashPulses), pcntUnit(unit), invert(invert) {}
  
  void begin() {
    // X4 quadrature decoding using both PCNT channels of one unit.
    //
    // Forward sequence (A,B): 00 -> 10 -> 11 -> 01 -> 00
    //   ch0: A rising  while B low   -> DEC reversed by lctrl -> +1
    //        A falling while B high  -> INC kept by hctrl     -> +1
    //   ch1: B rising  while A high  -> INC kept by hctrl     -> +1
    //        B falling while A low   -> DEC reversed by lctrl -> +1
    // All four edges move the counter the same way, so one cycle = 4 counts.
    pcnt_config_t pcnt_config = {
      .pulse_gpio_num = pinA,           // Signal A as pulse input
      .ctrl_gpio_num = pinB,            // Signal B as control
      .lctrl_mode = PCNT_MODE_REVERSE,  // Invert when B is low
      .hctrl_mode = PCNT_MODE_KEEP,     // Keep when B is high
      .pos_mode = PCNT_COUNT_DEC,       // Rising edge of A
      .neg_mode = PCNT_COUNT_INC,       // Falling edge of A
      .counter_h_lim = 32767,
      .counter_l_lim = -32768,
      .unit = pcntUnit,
      .channel = PCNT_CHANNEL_0,
    };
    pcnt_unit_config(&pcnt_config);
    
    // Channel 1: pulse on B, direction from A, base modes swapped vs channel 0
    pcnt_config.pulse_gpio_num = pinB;
    pcnt_config.ctrl_gpio_num = pinA;
    pcnt_config.channel = PCNT_CHANNEL_1;
    pcnt_config.pos_mode = PCNT_COUNT_INC;   // Rising edge of B
    pcnt_config.neg_mode = PCNT_COUNT_DEC;   // Falling edge of B
    pcnt_unit_config(&pcnt_config);
    
    // Glitch filter: ignore pulses shorter than 1us (80 APB cycles at 80MHz)
    pcnt_set_filter_value(pcntUnit, 80);
    pcnt_filter_enable(pcntUnit);
    
    // Let the 16-bit counter wrap instead of being cleared at the limits.
    // update() tracks int16 deltas, so wraparound is handled correctly and
    // no counts are lost.
    pcnt_event_disable(pcntUnit, PCNT_EVT_H_LIM);
    pcnt_event_disable(pcntUnit, PCNT_EVT_L_LIM);
    
    // pcnt_unit_config() routes the GPIOs but does not enable pullups.
    // The previous interrupt-based code relied on them, so keep them.
    gpio_pullup_en((gpio_num_t)pinA);
    gpio_pullup_en((gpio_num_t)pinB);
    
    pcnt_counter_pause(pcntUnit);
    pcnt_counter_clear(pcntUnit);
    pcnt_counter_resume(pcntUnit);
    
    // Seed lastCount so the first update() does not report a bogus delta
    pcnt_get_counter_value(pcntUnit, &lastCount);
  }
  
  void update() {
    int16_t pcntCount;
    pcnt_get_counter_value(pcntUnit, &pcntCount);
    
    // Delta since last read. Computed in uint16 then reinterpreted as signed
    // so the hardware counter wrapping at +-32768 is handled correctly.
    int16_t delta = (int16_t)((uint16_t)pcntCount - (uint16_t)lastCount);
    lastCount = pcntCount;
    
    if (delta == 0) return;
    
    // Direction of this batch, flipped if this axis is inverted so that
    // backlash compensation tracks the direction the operator actually sees.
    bool current_dir = invert ? (delta < 0) : (delta > 0);
    
    if (current_dir != dir) {
      dir = current_dir;
      backlash_counter = 0; // Direction reversed, start absorbing slop again
    }
    
    // Every pulse in the batch moves the same way, so split it in one step:
    // the first pulses fill the remaining backlash, the rest move the output.
    int pulses = abs(delta);
    int absorbed = min(pulses, backlashPulses - backlash_counter);
    backlash_counter += absorbed;
    count += current_dir ? (pulses - absorbed) : -(pulses - absorbed);
  }
  
  long getCount() const { return count; }
};

// --- ENCODER INSTANCES ---
EncoderAxis axisX(ENCODER_X_A, ENCODER_X_B, BACKLASH_X_PULSES, PCNT_UNIT_0, INVERT_X);
EncoderAxis axisY(ENCODER_Y_A, ENCODER_Y_B, BACKLASH_Y_PULSES, PCNT_UNIT_1, INVERT_Y);
EncoderAxis axisZ(ENCODER_Z_A, ENCODER_Z_B, BACKLASH_Z_PULSES, PCNT_UNIT_2, INVERT_Z);

// --- TACHO READER ---
TachoReader tachoReader(TACHO_RX_PIN);

BluetoothSerial SerialBT;

// --- TIMER VARIABLES ---
unsigned long lastSendTime = 0;
const unsigned long sendInterval = 40; // 40ms = ~25Hz refresh rate for TouchDRO


void setup() {
  Serial.begin(115200);
  
  SerialBT.begin("Sherline_DRO");

  axisX.begin();
  axisY.begin();
  axisZ.begin();
  tachoReader.begin();
}

void loop() {

  // Update encoder counts from PCNT hardware
  axisX.update();
  axisY.update();
  axisZ.update();

  // Get encoder counts
  long snap_out_x = axisX.getCount();
  long snap_out_y = axisY.getCount();
  long snap_out_z = axisZ.getCount();

  // Update tacho reading
  tachoReader.loop();
  int current_rpm = tachoReader.getRPM();

  // Stream formatted data block to TouchDRO over Bluetooth at 25 Hz
  if (millis() - lastSendTime >= sendInterval) {
    lastSendTime = millis();
    
    SerialBT.print("x");SerialBT.print(snap_out_x);SerialBT.println(";");
    SerialBT.print("y");SerialBT.print(snap_out_y);SerialBT.println(";");
    SerialBT.print("z");SerialBT.print(snap_out_z);SerialBT.println(";");
    SerialBT.print("t");SerialBT.print(current_rpm);SerialBT.println(";");
  }
}
