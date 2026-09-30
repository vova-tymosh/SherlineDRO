#ifndef TACHO_H
#define TACHO_H

#include <Arduino.h>

// TM1652 Display Sniffer - Reads RPM from a TM1652 7-segment display via serial
class TachoReader {
private:
  int rxPin;
  int baudRate;
  
  // Internal buffer for display registers (TM1652 supports addresses 0x08 to 0x0D)
  uint8_t displayDigits[6];
  uint8_t currentAddr;
  bool expectingData;
  unsigned long lastPacketTime;
  unsigned long lastPrint;
  int currentRPM;

  // Lookup table to decode 7-segment bitmasks into ASCII characters.
  // Standard segment mapping: 
  // Bit 0 = a, Bit 1 = b, Bit 2 = c, Bit 3 = d, 
  // Bit 4 = e, Bit 5 = f, Bit 6 = g, Bit 7 = dp
  char decodeSegment(uint8_t mask) {
    // Strip the decimal point for digit matching
    uint8_t raw = mask & 0x7F;

    switch (raw) {
      case 0x3F: return '0';
      case 0x06: return '1';
      case 0x5B: return '2';
      case 0x4F: return '3';
      case 0x66: return '4';
      case 0x6D: return '5';
      case 0x7D: return '6';
      case 0x07: return '7';
      case 0x7F: return '8';
      case 0x6F: return '9';
      case 0x40: return '-';
      case 0x00: return ' '; // Blank / leading zero suppression
      default:   return '?'; // Unmapped symbol or custom glyph
    }
  }

  void parseDisplay() {
    char text[7] = {0};

    for (int i = 0; i < 4; i++) {
      text[i] = decodeSegment(displayDigits[i]);
    }
    text[4] = '\0';

    // Parse as integer RPM
    currentRPM = atoi(text);
  }

public:
  TachoReader(int rxPin, int baudRate = 19200) 
    : rxPin(rxPin), baudRate(baudRate), currentAddr(0), 
      expectingData(false), lastPacketTime(0), lastPrint(0), currentRPM(0) {
    for (int i = 0; i < 6; i++) {
      displayDigits[i] = 0;
    }
  }

  void begin() {
    // TM1652 uses 19200 8N1
    Serial2.begin(baudRate, SERIAL_8N1, rxPin, -1, true);
  }

  void loop() {
    while (Serial2.available() > 0) {
      uint8_t b = Serial2.read();
      lastPacketTime = millis();

      // Check if byte is an address command (0x08 - 0x0D corresponds to Digits 1 to 6)
      if (b >= 0x08 && b <= 0x0D) {
        currentAddr = b - 0x08;
        expectingData = true;
      } 
      // Check if byte is a control/brightness command (starts with high nibble 0x1_)
      else if ((b & 0xF0) == 0x10) {
        expectingData = false;
      } 
      // Otherwise, treat as segment pattern data
      else if (expectingData) {
        if (currentAddr < 6) {
          displayDigits[currentAddr] = b;
          currentAddr++; // Auto-increment for consecutive byte bursts
        }
      }
    }

    // Once the transmission pauses for > 20ms, parse the updated frame
    if (millis() - lastPacketTime > 20 && millis() - lastPrint > 100) {
      parseDisplay();
      lastPrint = millis();
    }
  }

  int getRPM() {
    return currentRPM;
  }
};

#endif // TACHO_H
