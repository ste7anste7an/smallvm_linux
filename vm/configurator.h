#pragma once

#include <Arduino.h>
#include <LittleFS.h>
#include <stdint.h>

#ifndef CONFIG_FILE
#define CONFIG_FILE "/config.txt"
#endif


// ---------------- Pin semantics ----------------
//
//  0   = unused (default / zero-init)
// -1   = GPIO 0 (explicit)
// >0   = GPIO N
//
using Pin = int;

constexpr Pin PIN_UNUSED = 0;
constexpr Pin PIN_ZERO   = -1;

// ---------------- Pin adapter ----------------
//
// Converts internal pin semantics to driver semantics
//   internal 0   -> -1 (unused)
//   internal -1  ->  0 (GPIO 0)
//   internal N   ->  N
//
constexpr int toGPIO(Pin p) {
    return (p == PIN_UNUSED) ? -1
         : (p == PIN_ZERO)   ?  0
         : p;
}

// ---------------- Config structures ----------------

struct Config {

    struct {
        char controller[16];
        int  spi;
        Pin  mosi;
        Pin  miso;
        Pin  sclk;
        Pin  cs;
        Pin  dc;
        Pin  rst;
        int  rotation;
        bool colorBGR;
        bool invert;
        Pin  backlight;
        int  width;
        int  height;
        int  col_offset;
        int  row_offset;
    } lcd;

    struct {
        int width;
        int height;
    } lvgl;

    struct {
        char controller[16];
        char interface[8];
        int  spi;
        int  i2c;
        Pin  irq;
        Pin  miso;
        Pin  mosi;
        Pin  sclk;
        Pin  cs;
        int  rotation;
        Pin  sda;
        Pin  scl;
        bool flip_x;
        bool flip_y;
        bool flip_x_y;
    } touch;

    struct {
        Pin sda;
        Pin scl;
        Pin rx_pin;
        Pin tx_pin;
    } other;
};

// ---------------- API ----------------

namespace configurator {

void setWarnUnknownKeys(bool enable);
void setDebug(bool enable);

bool loadConfig(Config* cfg);

// helper
inline int resolvePin(Pin p) {
    return (p == PIN_ZERO) ? 0 : p;
}

} // namespace configurator
