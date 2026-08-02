# Sherline DRO 

A simple and cheap device to use a tablet as your DRO for Sherline lathe/mill. Project includes schematic, all files to fabricate the device as well as device firmware. 

The idea is to read OEM sensor data from Sherline rotary encoders for axis as well as a tachometer output and tunnel that over Bluetooth to a tablet running TouchDRO.

The brain of the solution is a ESP32 board. I'm using this one - https://www.amazon.com/dp/B0D1V336DL?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1. But any other "D1 mini" compatible will do. The main thing - it has to have classic Bluetooth, TouchDRO doesn't talk over newer Bluetooth Low Energy (BLE). You'd need Arduino Studio to upload firmware (the .ino file) to the board. I'm using jlcpcb.com for fabrication, but other services might be ok as well.

The code is set of a backlash of 1 thou for each axis. You may need to tune it for your lathe/mill (see BACKLASH_X_PULSES and below). Axis works perfectly, tachometer might need some further tuning. 

To connect this device to your tablet, power on the device, in tablet Bluetooth settings pair with new device "Sherline_DRO". Then select the same device in TouchDRO. If you are planning to use more than one device at a time - before flashing firmware, change the device name in line "SerialBT.begin("Sherline_DRO");".

Enjoy!
