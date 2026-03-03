#define _GNU_SOURCE
#include "arduino_gfx.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <sys/mman.h>
#include <errno.h>

/* ---------- RGB565 helpers ---------- */
static inline uint8_t rgb565_r(uint16_t c) { return (uint8_t)((((c >> 11) & 0x1F) * 255 + 15) / 31); }
static inline uint8_t rgb565_g(uint16_t c) { return (uint8_t)((((c >>  5) & 0x3F) * 255 + 31) / 63); }
static inline uint8_t rgb565_b(uint16_t c) { return (uint8_t)((((c >>  0) & 0x1F) * 255 + 15) / 31); }

uint16_t ArduinoGFX_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}

/* ---------- Tiny 5x7 font (ASCII 0x20..0x7F), 5 bytes per char ---------- */
static const uint8_t font5x7[] = {
    0x00,0x00,0x00,0x00,0x00, 0x00,0x00,0x5F,0x00,0x00, 0x00,0x07,0x00,0x07,0x00, 0x14,0x7F,0x14,0x7F,0x14,
    0x24,0x2A,0x7F,0x2A,0x12, 0x23,0x13,0x08,0x64,0x62, 0x36,0x49,0x55,0x22,0x50, 0x00,0x05,0x03,0x00,0x00,
    0x00,0x1C,0x22,0x41,0x00, 0x00,0x41,0x22,0x1C,0x00, 0x14,0x08,0x3E,0x08,0x14, 0x08,0x08,0x3E,0x08,0x08,
    0x00,0x50,0x30,0x00,0x00, 0x08,0x08,0x08,0x08,0x08, 0x00,0x60,0x60,0x00,0x00, 0x20,0x10,0x08,0x04,0x02,
    0x3E,0x51,0x49,0x45,0x3E, 0x00,0x42,0x7F,0x40,0x00, 0x42,0x61,0x51,0x49,0x46, 0x21,0x41,0x45,0x4B,0x31,
    0x18,0x14,0x12,0x7F,0x10, 0x27,0x45,0x45,0x45,0x39, 0x3C,0x4A,0x49,0x49,0x30, 0x01,0x71,0x09,0x05,0x03,
    0x36,0x49,0x49,0x49,0x36, 0x06,0x49,0x49,0x29,0x1E, 0x00,0x36,0x36,0x00,0x00, 0x00,0x56,0x36,0x00,0x00,
    0x08,0x14,0x22,0x41,0x00, 0x14,0x14,0x14,0x14,0x14, 0x00,0x41,0x22,0x14,0x08, 0x02,0x01,0x51,0x09,0x06,
    0x32,0x49,0x79,0x41,0x3E, 0x7E,0x11,0x11,0x11,0x7E, 0x7F,0x49,0x49,0x49,0x36, 0x3E,0x41,0x41,0x41,0x22,
    0x7F,0x41,0x41,0x22,0x1C, 0x7F,0x49,0x49,0x49,0x41, 0x7F,0x09,0x09,0x09,0x01, 0x3E,0x41,0x49,0x49,0x7A,
    0x7F,0x08,0x08,0x08,0x7F, 0x00,0x41,0x7F,0x41,0x00, 0x20,0x40,0x41,0x3F,0x01, 0x7F,0x08,0x14,0x22,0x41,
    0x7F,0x40,0x40,0x40,0x40, 0x7F,0x02,0x0C,0x02,0x7F, 0x7F,0x04,0x08,0x10,0x7F, 0x3E,0x41,0x41,0x41,0x3E,
    0x7F,0x09,0x09,0x09,0x06, 0x3E,0x41,0x51,0x21,0x5E, 0x7F,0x09,0x19,0x29,0x46, 0x46,0x49,0x49,0x49,0x31,
    0x01,0x01,0x7F,0x01,0x01, 0x3F,0x40,0x40,0x40,0x3F, 0x1F,0x20,0x40,0x20,0x1F, 0x3F,0x40,0x38,0x40,0x3F,
    0x63,0x14,0x08,0x14,0x63, 0x07,0x08,0x70,0x08,0x07, 0x61,0x51,0x49,0x45,0x43, 0x00,0x7F,0x41,0x41,0x00,
    0x02,0x04,0x08,0x10,0x20, 0x00,0x41,0x41,0x7F,0x00, 0x04,0x02,0x01,0x02,0x04, 0x40,0x40,0x40,0x40,0x40,
    0x00,0x01,0x02,0x04,0x00, 0x20,0x54,0x54,0x54,0x78, 0x7F,0x48,0x44,0x44,0x38, 0x38,0x44,0x44,0x44,0x20,
    0x38,0x44,0x44,0x48,0x7F, 0x38,0x54,0x54,0x54,0x18, 0x08,0x7E,0x09,0x01,0x02, 0x0C,0x52,0x52,0x52,0x3E,
    0x7F,0x08,0x04,0x04,0x78, 0x00,0x44,0x7D,0x40,0x00, 0x20,0x40,0x44,0x3D,0x00, 0x7F,0x10,0x28,0x44,0x00,
    0x00,0x41,0x7F,0x40,0x00, 0x7C,0x04,0x18,0x04,0x78, 0x7C,0x08,0x04,0x04,0x78, 0x38,0x44,0x44,0x44,0x38,
    0x7C,0x14,0x14,0x14,0x08, 0x08,0x14,0x14,0x18,0x7C, 0x7C,0x08,0x04,0x04,0x08, 0x48,0x54,0x54,0x54,0x20,
    0x04,0x3F,0x44,0x40,0x20, 0x3C,0x40,0x40,0x20,0x7C, 0x1C,0x20,0x40,0x20,0x1C, 0x3C,0x40,0x30,0x40,0x3C,
    0x44,0x28,0x10,0x28,0x44, 0x0C,0x50,0x50,0x50,0x3C, 0x44,0x64,0x54,0x4C,0x44, 0x00,0x08,0x36,0x41,0x00,
    0x00,0x00,0x7F,0x00,0x00, 0x00,0x41,0x36,0x08,0x00, 0x02,0x01,0x02,0x04,0x02, 0x00,0x00,0x00,0x00,0x00
};

/* ---------- low-level pixel (BGR888 framebuffer) ---------- */
static inline void put_pixel_bgr888(ArduinoGFX *tft, int x, int y, uint8_t r, uint8_t g, uint8_t b)
{
    if((unsigned)x >= (unsigned)tft->width || (unsigned)y >= (unsigned)tft->height) return;
    uint8_t *p = tft->fb + (size_t)y * (size_t)tft->stride_bytes + (size_t)x * 3u;
    p[0] = b; p[1] = g; p[2] = r;
}
static inline void put_pixel565(ArduinoGFX *tft, int x, int y, uint16_t c)
{
    put_pixel_bgr888(tft, x, y, rgb565_r(c), rgb565_g(c), rgb565_b(c));
}

/* ---------- mapping ---------- */
static int map_fb(ArduinoGFX *tft)
{
    size_t fb_bytes = (size_t)tft->stride_bytes * (size_t)tft->height;

    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if(fd < 0) { perror("open(/dev/mem)"); return -1; }

    long page = sysconf(_SC_PAGESIZE);
    off_t phys = (off_t)tft->phys_base;
    off_t phys_page = phys & ~(off_t)(page - 1);
    off_t page_off  = phys - phys_page;

    tft->map_bytes = fb_bytes + (size_t)page_off;
    tft->map_base = mmap(NULL, tft->map_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys_page);
    close(fd);

    if(tft->map_base == MAP_FAILED) {
        tft->map_base = NULL;
        perror("mmap");
        return -1;
    }

    tft->fb = (uint8_t*)tft->map_base + page_off;
    return 0;
}

static void unmap_fb(ArduinoGFX *tft)
{
    if(tft->map_base) {
        munmap(tft->map_base, tft->map_bytes);
        tft->map_base = NULL;
        tft->fb = NULL;
        tft->map_bytes = 0;
    }
}

/* ---------- methods ---------- */
static bool m_begin(ArduinoGFX *tft)
{
    if(map_fb(tft) != 0) return false;

    tft->cursor_x = 0;
    tft->cursor_y = 0;
    tft->text_fg = 0xFFFF;
    tft->text_bg = 0x0000;
    tft->text_bg_enabled = false;
    tft->text_size = 1;
    tft->text_wrap = true;
    return true;
}

static void m_end(ArduinoGFX *tft) { unmap_fb(tft); }

static void m_flush(ArduinoGFX *tft)
{
    if(tft->map_base) msync(tft->map_base, tft->map_bytes, MS_SYNC);
}

static void m_drawPixel(ArduinoGFX *tft, int x, int y, uint16_t c) { put_pixel565(tft, x, y, c); }

/* Bresenham */
static void m_drawLine(ArduinoGFX *tft, int x0,int y0,int x1,int y1, uint16_t c)
{
    int dx = abs(x1-x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1-y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while(1) {
        m_drawPixel(tft, x0, y0, c);
        if(x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if(e2 >= dy) { err += dy; x0 += sx; }
        if(e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void m_drawRect(ArduinoGFX *tft, int x,int y,int w,int h, uint16_t c)
{
    if(w <= 0 || h <= 0) return;
    m_drawLine(tft, x, y, x+w-1, y, c);
    m_drawLine(tft, x, y+h-1, x+w-1, y+h-1, c);
    m_drawLine(tft, x, y, x, y+h-1, c);
    m_drawLine(tft, x+w-1, y, x+w-1, y+h-1, c);
}

static void m_fillRect(ArduinoGFX *tft, int x,int y,int w,int h, uint16_t c)
{
    if(w <= 0 || h <= 0) return;

    int x0=x, y0=y, x1=x+w-1, y1=y+h-1;
    if(x0 < 0) x0 = 0;
    if(y0 < 0) y0 = 0;
    if(x1 >= tft->width)  x1 = tft->width-1;
    if(y1 >= tft->height) y1 = tft->height-1;

    uint8_t r = rgb565_r(c), g = rgb565_g(c), b = rgb565_b(c);

    for(int yy=y0; yy<=y1; yy++) {
        uint8_t *row = tft->fb + (size_t)yy*(size_t)tft->stride_bytes + (size_t)x0*3u;
        for(int xx=x0; xx<=x1; xx++) {
            row[0]=b; row[1]=g; row[2]=r;
            row += 3;
        }
    }
}

static void m_fillScreen(ArduinoGFX *tft, uint16_t c)
{
    m_fillRect(tft, 0, 0, tft->width, tft->height, c);
}

/* Circles */
static void m_drawCircle(ArduinoGFX *tft, int x0,int y0,int r, uint16_t c)
{
    int x=0, y=r, d=1-r;
    while(y >= x) {
        m_drawPixel(tft, x0+x, y0+y, c);
        m_drawPixel(tft, x0+y, y0+x, c);
        m_drawPixel(tft, x0-x, y0+y, c);
        m_drawPixel(tft, x0-y, y0+x, c);
        m_drawPixel(tft, x0+x, y0-y, c);
        m_drawPixel(tft, x0+y, y0-x, c);
        m_drawPixel(tft, x0-x, y0-y, c);
        m_drawPixel(tft, x0-y, y0-x, c);
        x++;
        if(d < 0) d += 2*x + 1;
        else { y--; d += 2*(x-y) + 1; }
    }
}

static void m_fillCircle(ArduinoGFX *tft, int x0,int y0,int r, uint16_t c)
{
    int x=0, y=r, d=1-r;
    while(y >= x) {
        m_drawLine(tft, x0-y, y0+x, x0+y, y0+x, c);
        m_drawLine(tft, x0-y, y0-x, x0+y, y0-x, c);
        m_drawLine(tft, x0-x, y0+y, x0+x, y0+y, c);
        m_drawLine(tft, x0-x, y0-y, x0+x, y0-y, c);
        x++;
        if(d < 0) d += 2*x + 1;
        else { y--; d += 2*(x-y) + 1; }
    }
}

/* Triangles */
static void swap_int(int *a,int *b){ int t=*a; *a=*b; *b=t; }

static void m_drawTriangle(ArduinoGFX *tft, int x0,int y0,int x1,int y1,int x2,int y2, uint16_t c)
{
    m_drawLine(tft, x0,y0, x1,y1, c);
    m_drawLine(tft, x1,y1, x2,y2, c);
    m_drawLine(tft, x2,y2, x0,y0, c);
}

static void m_fillTriangle(ArduinoGFX *tft, int x0,int y0,int x1,int y1,int x2,int y2, uint16_t c)
{
    if(y0 > y1) { swap_int(&y0,&y1); swap_int(&x0,&x1); }
    if(y1 > y2) { swap_int(&y1,&y2); swap_int(&x1,&x2); }
    if(y0 > y1) { swap_int(&y0,&y1); swap_int(&x0,&x1); }

    if(y0 == y2) {
        int minx=x0, maxx=x0;
        if(x1<minx) minx=x1; if(x1>maxx) maxx=x1;
        if(x2<minx) minx=x2; if(x2>maxx) maxx=x2;
        m_drawLine(tft, minx, y0, maxx, y0, c);
        return;
    }

    int total_h = y2 - y0;
    for(int y=y0; y<=y2; y++) {
        bool second_half = y > y1 || y1 == y0;
        int segment_h = second_half ? (y2 - y1) : (y1 - y0);
        float alpha = (float)(y - y0) / (float)total_h;
        float beta  = (float)(y - (second_half ? y1 : y0)) / (float)segment_h;

        int ax = x0 + (int)lroundf((x2 - x0) * alpha);
        int bx = second_half ? (x1 + (int)lroundf((x2 - x1) * beta))
                             : (x0 + (int)lroundf((x1 - x0) * beta));
        if(ax > bx) swap_int(&ax,&bx);
        m_drawLine(tft, ax, y, bx, y, c);
    }
}

/* Round rects */
static void m_drawRoundRect(ArduinoGFX *tft, int x,int y,int w,int h,int r, uint16_t c)
{
    if(w<=0 || h<=0) return;
    if(r<0) r=0;
    if(r>w/2) r=w/2;
    if(r>h/2) r=h/2;

    m_drawLine(tft, x+r, y, x+w-r-1, y, c);
    m_drawLine(tft, x+r, y+h-1, x+w-r-1, y+h-1, c);
    m_drawLine(tft, x, y+r, x, y+h-r-1, c);
    m_drawLine(tft, x+w-1, y+r, x+w-1, y+h-r-1, c);

    int xx=0, yy=r, d=1-r;
    while(yy >= xx) {
        int tlx=x+r, tly=y+r;
        int trx=x+w-r-1, try_=y+r;
        int blx=x+r, bly=y+h-r-1;
        int brx=x+w-r-1, bry=y+h-r-1;

        m_drawPixel(tft, tlx-yy, tly-xx, c);
        m_drawPixel(tft, tlx-xx, tly-yy, c);
        m_drawPixel(tft, trx+yy, try_-xx, c);
        m_drawPixel(tft, trx+xx, try_-yy, c);
        m_drawPixel(tft, blx-yy, bly+xx, c);
        m_drawPixel(tft, blx-xx, bly+yy, c);
        m_drawPixel(tft, brx+yy, bry+xx, c);
        m_drawPixel(tft, brx+xx, bry+yy, c);

        xx++;
        if(d < 0) d += 2*xx + 1;
        else { yy--; d += 2*(xx-yy) + 1; }
    }
}

static void m_fillRoundRect(ArduinoGFX *tft, int x,int y,int w,int h,int r, uint16_t c)
{
    if(w<=0 || h<=0) return;
    if(r<0) r=0;
    if(r>w/2) r=w/2;
    if(r>h/2) r=h/2;

    m_fillRect(tft, x+r, y, w-2*r, h, c);
    m_fillRect(tft, x, y+r, r, h-2*r, c);
    m_fillRect(tft, x+w-r, y+r, r, h-2*r, c);

    int xx=0, yy=r, d=1-r;
    while(yy >= xx) {
        m_drawLine(tft, x+r-yy, y+r-xx, x+w-r-1+yy, y+r-xx, c);
        m_drawLine(tft, x+r-xx, y+r-yy, x+w-r-1+xx, y+r-yy, c);

        m_drawLine(tft, x+r-yy, y+h-r-1+xx, x+w-r-1+yy, y+h-r-1+xx, c);
        m_drawLine(tft, x+r-xx, y+h-r-1+yy, x+w-r-1+xx, y+h-r-1+yy, c);

        xx++;
        if(d < 0) d += 2*xx + 1;
        else { yy--; d += 2*(xx-yy) + 1; }
    }
}

/* Bitmap blit: RGB565 input */
static void m_draw16bitRGBBitmap(ArduinoGFX *tft, int x,int y, const uint16_t *bmp, int w,int h)
{
    if(!bmp || w<=0 || h<=0) return;

    int x0=x, y0=y, x1=x+w-1, y1=y+h-1;
    if(x1 < 0 || y1 < 0 || x0 >= tft->width || y0 >= tft->height) return;

    int sx0=0, sy0=0;
    if(x0 < 0) { sx0 = -x0; x0 = 0; }
    if(y0 < 0) { sy0 = -y0; y0 = 0; }
    if(x1 >= tft->width)  x1 = tft->width-1;
    if(y1 >= tft->height) y1 = tft->height-1;

    int dw = x1 - x0 + 1;
    int dh = y1 - y0 + 1;

    for(int row=0; row<dh; row++) {
        const uint16_t *src = bmp + (size_t)(sy0+row)*(size_t)w + (size_t)sx0;
        uint8_t *dst = tft->fb + (size_t)(y0+row)*(size_t)tft->stride_bytes + (size_t)x0*3u;
        for(int col=0; col<dw; col++) {
            uint16_t c = src[col];
            dst[0] = rgb565_b(c);
            dst[1] = rgb565_g(c);
            dst[2] = rgb565_r(c);
            dst += 3;
        }
    }
}

/* Text */
static void m_setCursor(ArduinoGFX *tft, int x,int y){ tft->cursor_x=x; tft->cursor_y=y; }
static void m_setTextColor(ArduinoGFX *tft, uint16_t fg, uint16_t bg){ tft->text_fg=fg; tft->text_bg=bg; tft->text_bg_enabled=true; }
static void m_setTextColorNoBG(ArduinoGFX *tft, uint16_t fg){ tft->text_fg=fg; tft->text_bg_enabled=false; }
static void m_setTextSize(ArduinoGFX *tft, int s){ tft->text_size = (s<1)?1:s; }
static void m_setTextWrap(ArduinoGFX *tft, bool w){ tft->text_wrap=w; }

static void draw_char(ArduinoGFX *tft, int x,int y, char ch)
{
    unsigned char c = (unsigned char)ch;
    if(c < 0x20 || c > 0x7F) c = '?';
    int idx = ((int)c - 0x20) * 5;

    int size = tft->text_size;
    int cw = 6*size;
    int chh = 8*size;

    if(tft->text_bg_enabled) m_fillRect(tft, x, y, cw, chh, tft->text_bg);

    for(int col=0; col<5; col++) {
        uint8_t bits = font5x7[idx + col];
        for(int row=0; row<7; row++) {
            if(bits & (1u<<row)) {
                if(size==1) m_drawPixel(tft, x+col, y+row, tft->text_fg);
                else m_fillRect(tft, x+col*size, y+row*size, size, size, tft->text_fg);
            }
        }
    }
}

static void m_print(ArduinoGFX *tft, const char *s)
{
    if(!s) return;
    int size = tft->text_size;
    int cw = 6*size;
    int chh = 8*size;

    while(*s) {
        char c = *s++;
        if(c=='\n') { tft->cursor_x=0; tft->cursor_y += chh; continue; }
        if(c=='\r') continue;

        if(tft->text_wrap && (tft->cursor_x + cw > tft->width)) {
            tft->cursor_x = 0;
            tft->cursor_y += chh;
        }
        draw_char(tft, tft->cursor_x, tft->cursor_y, c);
        tft->cursor_x += cw;
    }
}

/* ---------- constructor binds methods ---------- */
ArduinoGFX *ArduinoGFX_create(int width,int height,int stride_bytes,uint64_t phys_base)
{
    ArduinoGFX *tft = (ArduinoGFX*)calloc(1, sizeof(ArduinoGFX));
    if(!tft) return NULL;

    tft->width = width;
    tft->height = height;
    tft->stride_bytes = stride_bytes;
    tft->phys_base = phys_base;

    /* bind methods */
    tft->begin = m_begin;
    tft->end = m_end;
    tft->flush = m_flush;

    tft->draw16bitRGBBitmap = m_draw16bitRGBBitmap;

    tft->drawCircle = m_drawCircle;
    tft->drawLine = m_drawLine;
    tft->drawPixel = m_drawPixel;
    tft->drawRect = m_drawRect;
    tft->drawRoundRect = m_drawRoundRect;
    tft->drawTriangle = m_drawTriangle;

    tft->fillCircle = m_fillCircle;
    tft->fillRect = m_fillRect;
    tft->fillRoundRect = m_fillRoundRect;
    tft->fillScreen = m_fillScreen;
    tft->fillTriangle = m_fillTriangle;

    tft->print = m_print;
    tft->setCursor = m_setCursor;
    tft->setTextColor = m_setTextColor;
    tft->setTextColorNoBG = m_setTextColorNoBG;
    tft->setTextSize = m_setTextSize;
    tft->setTextWrap = m_setTextWrap;

    return tft;
}

void ArduinoGFX_destroy(ArduinoGFX *tft)
{
    if(!tft) return;
    if(tft->map_base) unmap_fb(tft);
    free(tft);
}
