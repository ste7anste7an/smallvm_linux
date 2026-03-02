#include "touch_universal.h"

/* ---------- FT62xx registers ---------- */
#define FT_REG_GESTURE_ID   0x01
#define FT_REG_NUM_TOUCHES  0x02
#define FT_REG_XH           0x03
#define FT_REG_XL           0x04
#define FT_REG_YH           0x05
#define FT_REG_YL           0x06

/* Optional extra I2C bus */
TwoWire I2C_1(1);

TwoWire* TouchUniversal::wire() {
  switch (_i2cIf) {
    case 1:  return &I2C_1;
    case 0:
    default: return &Wire;
  }
}

void TouchUniversal::configureCST820(
  int i2c_interface, int sda, int scl,
  uint8_t addr, uint32_t clock_hz, uint32_t poll_ms
) {
  _ctrl = TOUCH_CTRL_CST820;
  _i2cIf = i2c_interface;
  _sda = sda;
  _scl = scl;
  _addr = addr;
  _clk = clock_hz;
  _pollMs = poll_ms;

  _wireStarted = false;
  _enabled = false;
  _x = _y = _g = -1;
}

void TouchUniversal::configureFT62XX(
  int i2c_interface, int sda, int scl,
  uint8_t addr, uint32_t clock_hz, uint32_t poll_ms
) {
  _ctrl = TOUCH_CTRL_FT62XX;
  _i2cIf = i2c_interface;
  _sda = sda;
  _scl = scl;
  _addr = addr;
  _clk = clock_hz;
  _pollMs = poll_ms;

  _wireStarted = false;
  _enabled = false;
  _x = _y = _g = -1;
}

void TouchUniversal::startWire() {
  if (_wireStarted) return;

  TwoWire* TW = wire();
  TW->end();
  TW->setPins(_sda, _scl);
  TW->begin();
  TW->setClock(_clk);

  _wireStarted = true;
}

void TouchUniversal::begin() {
  if (_enabled) return;
  startWire();
  if (!_wireStarted) return;

  if (_ctrl == TOUCH_CTRL_CST820) {
    writeReg(0xFE, 0xFF); // CST820: do not sleep
  }

  _enabled = true;
}

int TouchUniversal::readReg(uint8_t reg) {
  startWire();
  TwoWire* TW = wire();

  TW->beginTransmission(_addr);
  TW->write(reg);
  if (TW->endTransmission(false) != 0) return -1;

  TW->requestFrom((int)_addr, 1);
  return TW->available() ? TW->read() : -1;
}

void TouchUniversal::writeReg(uint8_t reg, uint8_t val) {
  startWire();
  TwoWire* TW = wire();

  TW->beginTransmission(_addr);
  TW->write(reg);
  TW->write(val);
  TW->endTransmission(true);
}

int TouchUniversal::touched() {
  begin();

  if (_ctrl == TOUCH_CTRL_CST820) {
    int n = readReg(0x02);
    return (n >= 1 && n <= 5) ? n : 0;
  }

  if (_ctrl == TOUCH_CTRL_FT62XX) {
    int n = readReg(FT_REG_NUM_TOUCHES);
    return (n >= 1 && n <= 2) ? n : 0;
  }

  return 0;
}

void TouchUniversal::update() {
  begin();
  if (!_enabled) return;

  uint32_t now = millis();
  if ((uint32_t)(now - _lastUpdate) < _pollMs) return;

  if (_ctrl == TOUCH_CTRL_CST820)
    updateCST820();
  else if (_ctrl == TOUCH_CTRL_FT62XX)
    updateFT62XX();

  _lastUpdate = now;
}

/* ---------- CST820 ---------- */
void TouchUniversal::updateCST820() {
  if (!touched()) {
    _x = _y = _g = -1;
    return;
  }

  uint8_t d[6];
  TwoWire* TW = wire();

  TW->beginTransmission(_addr);
  TW->write((uint8_t)0x01);
  TW->endTransmission(false);
  TW->requestFrom((int)_addr, 6);

  for (uint8_t i = 0; i < 6; i++)
    d[i] = TW->available() ? TW->read() : 0;

  _g = d[0];
  _y = ((d[2] & 0x0F) << 8) | d[3];
  _x = ((d[4] & 0x0F) << 8) | d[5];
}

/* ---------- FT62xx ---------- */
void TouchUniversal::updateFT62XX() {
  if (!touched()) {
    _x = _y = _g = -1;
    return;
  }

  int xh = readReg(FT_REG_XH);
  if ((xh >> 6) == 1) {
    _x = _y = -1;
    return;
  }

  int xl = readReg(FT_REG_XL);
  int yh = readReg(FT_REG_YH);
  int yl = readReg(FT_REG_YL);

  _x = ((xh & 0x0F) << 8) | xl;
  _y = ((yh & 0x0F) << 8) | yl;
  _g = readReg(FT_REG_GESTURE_ID);
}

int TouchUniversal::x()       { update(); return _x; }
int TouchUniversal::y()       { update(); return _y; }
int TouchUniversal::gesture() { update(); return _g; }

int TouchUniversal::pressure() {
  return touched() ? 1000 : -1;
}
