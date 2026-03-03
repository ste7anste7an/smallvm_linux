#ifndef ARDUINO_GFX_H
#define ARDUINO_GFX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ArduinoGFX ArduinoGFX;

/* "Object" with Arduino-style methods */
struct ArduinoGFX {
    /* config */
    int width;
    int height;
    int stride_bytes;
    uint64_t phys_base;

    /* internal state (opaque-ish, but kept here for simplicity) */
    uint8_t *fb;
    void    *map_base;
    size_t   map_bytes;

    /* text state */
    int cursor_x, cursor_y;
    uint16_t text_fg;
    uint16_t text_bg;
    bool text_bg_enabled;
    int text_size;
    bool text_wrap;

    /* methods (pass tft as first argument) */
    bool (*begin)(ArduinoGFX *tft);
    void (*end)(ArduinoGFX *tft);
    void (*flush)(ArduinoGFX *tft);

    void (*draw16bitRGBBitmap)(ArduinoGFX *tft, int x, int y, const uint16_t *bitmap565, int w, int h);

    void (*drawCircle)(ArduinoGFX *tft, int x0, int y0, int r, uint16_t color565);
    void (*drawLine)(ArduinoGFX *tft, int x0, int y0, int x1, int y1, uint16_t color565);
    void (*drawPixel)(ArduinoGFX *tft, int x, int y, uint16_t color565);
    void (*drawRect)(ArduinoGFX *tft, int x, int y, int w, int h, uint16_t color565);
    void (*drawRoundRect)(ArduinoGFX *tft, int x, int y, int w, int h, int r, uint16_t color565);
    void (*drawTriangle)(ArduinoGFX *tft, int x0,int y0,int x1,int y1,int x2,int y2, uint16_t color565);

    void (*fillCircle)(ArduinoGFX *tft, int x0, int y0, int r, uint16_t color565);
    void (*fillRect)(ArduinoGFX *tft, int x, int y, int w, int h, uint16_t color565);
    void (*fillRoundRect)(ArduinoGFX *tft, int x, int y, int w, int h, int r, uint16_t color565);
    void (*fillScreen)(ArduinoGFX *tft, uint16_t color565);
    void (*fillTriangle)(ArduinoGFX *tft, int x0,int y0,int x1,int y1,int x2,int y2, uint16_t color565);

    void (*print)(ArduinoGFX *tft, const char *s);
    void (*setCursor)(ArduinoGFX *tft, int x, int y);
    void (*setTextColor)(ArduinoGFX *tft, uint16_t fg565, uint16_t bg565);
    void (*setTextColorNoBG)(ArduinoGFX *tft, uint16_t fg565);
    void (*setTextSize)(ArduinoGFX *tft, int size);
    void (*setTextWrap)(ArduinoGFX *tft, bool wrap);
};

/* constructor / destructor */
ArduinoGFX *ArduinoGFX_create(int width, int height, int stride_bytes, uint64_t phys_base);
void        ArduinoGFX_destroy(ArduinoGFX *tft);

/* helper */
uint16_t ArduinoGFX_rgb565(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif /* ARDUINO_GFX_H */
