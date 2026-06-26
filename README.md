STM32 Embedded Project:<br /> I2C Sensor Integration and Firmware Development<br />
======================================================================================================
Bare-metal embedded project on an STM32F401RE focused on sensor integration, I2C communication, UART-based diagnostics, and low-level debugging.


## Origin

This project is based on the following repository:
https://github.com/g-schro/ram-class-1-code


## Contributions

The following components were developed or significantly extended by me:

- Refactoring and extension of the I2C module and public API
- Implementation of I2C slave functionality
- Design and implementation of the ambient light sensor module (alsm)
- Bug fix in ttys module: UART RX buffer overrun caused interrupt storm.
- Python-based test automation for UART communication (work in progress)


## Hardware

- STM32 Nucleo-F401RE
- BH1750FVI digital ambient light sensor
- Breadboard prototype with I2C sensor integration
- Logic analyzer for signal inspection and debugging


## Motivation

This project was created to gain hands-on experience with embedded software development on STM32 microcontrollers.

Key topics explored:

- Low-level peripheral programming
- I2C communication
- Sensor integration
- Debugging and fault analysis
- Software architecture and modularization
- Embedded development toolchain (compiler, linker, build system, debugger)
