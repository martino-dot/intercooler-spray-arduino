# Intercooler Spray Arduino
A program for the Arduino Due that lets it control an intercooler sprayer in a Subaru STi. 

Intended setup is a developmental board on top of the Arduino to house a transistor that controls the incandescent bulb on the intercooler spray switch for feedback, and 3.3v relay board to control the pump. 

## Features
* Adjustable auto intercooler spray
* Can spray under boost, on a set interval, or manually
* Saves spray mode through power loss
* Cooldown spray
* Low water level warning and auto disable when level is low
* Refill detection and feedback
* Diagnostic features

## Required Dependencies
[DueFlashStorage](https://github.com/sebnil/DueFlashStorage)

## Pin Assignments
**Digital Pin 3** - Status LED Output (Linked to the Intercooler Spray Button Bulb)
**Digital Pin 5** -Low Level Sensor Input (Normally closed)
**Digital Pin 6** - Intercooler Spray Button Input (Or any other momentary button; Normally Open)
**Digital Pin 7** - HOBBs Switch Input (Used to determine if in boost; Normally Open)
**Digital Pin 8** -  Pump Relay Output

## Default parameters
**Single Button Press** - Whenever under boost, sprays for 2 seconds every 30 seconds. A cooldown spray is sprayed whenever car is under boost for more than 20 seconds in a 1 minute window.
**Double Button Press** - Sprays for 2 seconds every 30 seconds, regardless of boost. No boost tracking.
**Button Held Down** - If the button is held down for more than 2 seconds a *forced* spray will run, regardless of what mode it is in, until the button is let go. If water is low level, then this mode will NOT turn on and the status light will blink rapidly instead.
**Turning a Mode off** - Pressing the button **once** in a mode will turn it off.
**Switching Modes** - You **must** disable one of the modes before activating the other.  
**Additional Note** - Modes WILL be saved when the car is turned off and then back on. However, during startup delay, no mode will spray any water until it is done. If the water level is LOW then the mode will automatically be put into off.
**Startup Delay** - There is a 15 second delay before being able to control anything when turning the arduino/car on. During startup the status LED will blink once to signify for boost mode, blink twice for interval mode, and blink rapidly for low water, otherwise it will stay off.
**LED Status Blinks:**
1 Blink - Boost mode is ON
2 Blinks - Interval Mode is ON
3 Blinks - Mode 1 or 2 is turned off
5 Blinks - Tank Refilled
Rapid Blinks - Low Water Level
Constant On - Forced mode is running

## Diagnostics
When connected to the Arduino through the **programming USB port**, in serial console the following commands will report the following information:
**p** - Pump Relay Test (turns on pump for 1 second)
**l** - Status LED test (turns on LED for 1 second)
**m** - Lets you switch between modes (off, boost, and interval)
**d** - Reports current mode, time since last spray, and water status
