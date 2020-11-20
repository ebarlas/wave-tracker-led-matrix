# Overview
Wave Tracker LED Matrix is a desktop digital art piece that displays buoy observation metrics on
an LED matrix. It is be powered by an internet-connected Raspberry Pi Zero W and a 16x32 Adafruit LED matrix.
The system is driven by two separate software modules. First, a C++ program that scrolls an image across
the display using GPIO pins to communicate with the LED matrix. This software is built atop the 
[rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) library.
Second, a Python controller program that downloads wave information from NOAA
and produces a data file for the C++ program to consume. 

# Requirements

* Hardware
  * Raspberry Pi Zero W
  * Adafruit 16 x 32 LED Matrix
* Software
  * Raspbian or Raspberry Pi OS
  * Python 3
  * C++ 17
  * [rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix)
  
# This Repo

This repository contains the following:

### Sprite Assets
Raw sprite assets are SVG files and application assets are modeled with 
binary files of the following format consisting of unsigned 8-bit integer segments.

```
[num frames]
for i in 1..{num frames}:
  [width][height]
  for j in 1..{width * height}:
    [r][g][b] 
```

The `rasterize.py` script converts SVG files to bin files.

* `arrows` - 1 green up arrow, 1 red down arrow
* `wave` - 21 crashing wave frames
* `fadein` - 7 water rising frames
* `fadeout` - 6 water falling frames 

### C++ Controller

The `wave.cpp` C++ controller application reads a wave message file at start
up and then endlessly renders the messages on an LED matrix.

### Python Controller 

The `buoys.py` Python controller application reads a buoy station JSON file
at start up and then queries a NOAA web service minutely for recent buoy 
observations. Upon discovering a new observation, the Python controller 
restarts the C++ LED matrix controller.

# Build

...