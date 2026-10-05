Specs for INA226 Driver: 
Follow the datasheet given here: https://www.ti.com/lit/ds/symlink/ina226.pdf

This is the broader system spec: 
Outline
Control subcircuit: uses a I/O Expander (MCP23017) + SPDTs (ADG734) to control which pixel is being read from
Expects: I2C connection from MCP23017 to MCU, which controls which pixel is being read from
Guarantees: specified pixel is connected to measurement subcircuit while others are connected through resistors to ground
MCU uses I2C to tell MCP23017 which of 16 GPIO pins to set to high
Each GPIO pin connects to a ADG734 switch: when GPIO pin is high, the pixel’s positive terminal is connected to the measurement circuit, while when GPIO pin is low, the pixel’s positive terminal is connected through a resistor to ground
Measurement subcircuit:
Voltage control subcircuit: uses an opamp (TLV9001) + DAC line to control voltage across a given pixel
Expects: voltage from DAC ranging from 0-3.3V
Guarantees: voltage across a given pixel will follow DAC voltage proportionally from 0-1.2V
Voltage divider splits range from 0-3.3V to 0-1.2V
Opamp supplies/sinks necessary current for voltage across pixel to match DAC voltage
Reading subcircuit: uses a current/voltage monitor (INA226) to read voltage across & current through pixel
Expects: I2C connection from INA226 to MCU, which reports voltage & current (might be in form of shunt voltage, with shunt resistor value of 10 ohms)
Guarantees: reports voltage & current (see above)

Firmware
Use I2C to MCP23017 to start reading from a given pixel
Use DAC to step through control voltages from 0 to 3.3V
Use I2C to INA226 to read pixel voltage & current
MCP23017 address: 0100000
INA226 address: 1000000


