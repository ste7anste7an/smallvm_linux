// touch_cst820.h
#pragma once
#include <Arduino.h>
#include <Wire.h>
class TouchCST820 {
public:
  TouchCST820(uint8_t addr = 0x15) : _addr(addr) {}

  // Configure I2C instance and pins (0=Wire, 1=Wire1, 2=Wire2...)
  void configure(int i2c_interface, int sda, int scl, uint32_t clock_hz = 400000);

  void begin();                 // init chip (disable sleep)
  int  touched();               // number of touch points (0..5)
  void update();                // refresh cached x/y/gesture
  int  x();                     // last X, -1 if none
  int  y();                     // last Y, -1 if none
  int  gesture();               // last gesture, -1 if none
  int  pressure();              // constant 1000 if touched, else -1

  // Optional: adjust mapping
  void setScreenSize(int width, int height) { _w = width; _h = height; }
  void setPollIntervalMs(uint32_t ms) { _pollMs = ms; }
  

private:
  TwoWire* wire();
  void     startWire();
  int      readReg(uint8_t reg);
  void     writeReg(uint8_t reg, uint8_t val);

private:
  uint8_t  _addr;
  int      _i2cIf = 0;
  int      _sda = -1, _scl = -1;
  uint32_t _clk = 400000;

  bool     _wireStarted = false;
  bool     _enabled = false;

  uint32_t _lastUpdate = 0;
  uint32_t _pollMs = 10;

  int      _x = -1, _y = -1, _g = -1;

  // Used for your original Y flip mapping; set these from TFT dims
  int      _w = 240;
  int      _h = 240;
};
