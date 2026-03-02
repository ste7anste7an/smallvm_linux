#pragma once
#include <Arduino.h>
#include <Wire.h>

enum TouchController {
  TOUCH_CTRL_NONE = 0,
  TOUCH_CTRL_CST820,
  TOUCH_CTRL_FT62XX
};

class TouchUniversal {
public:
  /* CST820 */
  void configureCST820(
    int i2c_interface,
    int sda,
    int scl,
    uint8_t addr,
    uint32_t clock_hz = 400000,
    uint32_t poll_ms = 16
  );

  /* FT62xx */
  void configureFT62XX(
    int i2c_interface,
    int sda,
    int scl,
    uint8_t addr = 0x38,
    uint32_t clock_hz = 400000,
    uint32_t poll_ms = 16
  );

  int touched();
  int x();
  int y();
  int gesture();
  int pressure();

private:
  /* common */
  TwoWire* wire();
  void startWire();
  int  readReg(uint8_t reg);
  void writeReg(uint8_t reg, uint8_t val);
  void begin();
  void update();

  /* per-controller update */
  void updateCST820();
  void updateFT62XX();

  /* state */
  TouchController _ctrl = TOUCH_CTRL_NONE;

  int _i2cIf = 0;
  int _sda = -1;
  int _scl = -1;
  uint8_t _addr = 0;
  uint32_t _clk = 400000;

  bool _wireStarted = false;
  bool _enabled = false;

  uint32_t _pollMs = 16;
  uint32_t _lastUpdate = 0;

  int _x = -1;
  int _y = -1;
  int _g = -1;
};
