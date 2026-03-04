/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2020 John Maloney, Bernat Romagosa, and Jens Mönig
// linuxTftPrims.cpp - Microblocks TFT screen primitives using ArduinoGFX framebuffer backend
// Rewrite: replace SDL2 window/renderer with ArduinoGFX (/dev/mem framebuffer mapping)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <time.h>
#include <errno.h>
#include <linux/input.h>


#if defined(LVGL)
  #include <lvgl.h>





#endif


extern "C" {
  #include "mem.h"
  #include "interp.h"
}

#define DEFAULT_WIDTH  640
#define DEFAULT_HEIGHT 480

// If your physical framebuffer differs, set these at build time, e.g.:
//   -DTFT_WIDTH=800 -DTFT_HEIGHT=480 -DTFT_STRIDE=2400 -DTFT_PHYS_BASE=0x....
// STRIDE is bytes per line in the mapped framebuffer (BGR888 in your ArduinoGFX code => width*3 usually).
#ifndef TFT_WIDTH
#  define TFT_WIDTH DEFAULT_WIDTH
#endif
#ifndef TFT_HEIGHT
#  define TFT_HEIGHT DEFAULT_HEIGHT
#endif
#ifndef TFT_STRIDE
#  define TFT_STRIDE (TFT_WIDTH * 3)
#endif
#ifndef TFT_PHYS_BASE
#  define TFT_PHYS_BASE 0x15a00000ULL
#endif

// --- ArduinoGFX backend ---
#define _GNU_SOURCE
#include "arduino_gfx.h"

// --- Input simulation ---
// This backend writes to a framebuffer; it does not provide mouse/keyboard input.
// We keep the API but return "not touched" and leave KEY_SCANCODE cleared.
static int mouseDown = false;
static int mouseX = -1;
static int mouseY = -1;
static int mouseDownTime = 0;

static int tftEnabled = false;
static ArduinoGFX *tft = NULL;

extern int KEY_SCANCODE[];

// --- Helpers ---

static inline uint16_t color24_to_rgb565(int color24b) {
	uint8_t r = (color24b >> 16) & 255;
	uint8_t g = (color24b >>  8) & 255;
	uint8_t b = (color24b >>  0) & 255;
	return ArduinoGFX_rgb565(r, g, b);
}

static void initKeys() {
	// Keep behavior compatible with the old file.
	for (int i = 0; i < 255; i++) KEY_SCANCODE[i] = 0;
}

static void processEvents() {
	// No SDL event loop here. If you later add a touch/keyboard backend,
	// update mouseDown/mouseX/mouseY/mouseDownTime and KEY_SCANCODE[] here.
	(void)mouseDownTime;
	mouseDown = false;
	mouseX = -1;
	mouseY = -1;
}

// --- Public-ish API used elsewhere in MicroBlocks ---

void tftInit() {
	if (tftEnabled) return;

	tft = ArduinoGFX_create(TFT_WIDTH, TFT_HEIGHT, TFT_STRIDE, (uint64_t)TFT_PHYS_BASE);
	if (!tft) {
		printf("ArduinoGFX_create failed\n");
		return;
	}
	if (!tft->begin(tft)) {
		printf("ArduinoGFX begin() failed (check /dev/mem perms and phys addr)\n");
		ArduinoGFX_destroy(tft);
		tft = NULL;
		return;
	}

	// Clear screen on init
	tft->fillScreen(tft, ArduinoGFX_rgb565(0, 0, 0));
	tft->flush(tft);

	initKeys();
	tftEnabled = true;
}

void tftClear() {
	tftInit();
	if (!tftEnabled || !tft) return;
	tft->fillScreen(tft, ArduinoGFX_rgb565(0, 0, 0));
	tft->flush(tft);
}

void updateMicrobitDisplay() {
	// Old SDL version: processEvents() + present at ~60fps.
	// Here: processEvents() + flush each call (MicroBlocks typically calls this often).
	processEvents();
	if (tftEnabled && tft) {
		tft->flush(tft);
	}
}

// --- TFT Primitives (MicroBlocks "tft" primitive set) ---

static OBJ primEnableDisplay(int argCount, OBJ *args) {
	(void)argCount;
	if (trueObj == args[0]) {
		tftInit();
	} else {
		if (tft) {
			tft->end(tft);
			ArduinoGFX_destroy(tft);
			tft = NULL;
		}
		tftEnabled = false;
	}
	return falseObj;
}

static OBJ primGetWidth(int argCount, OBJ *args) {
	(void)argCount; (void)args;
	if (!tftEnabled || !tft) return zeroObj;
	return int2obj(tft->width);
}

static OBJ primGetHeight(int argCount, OBJ *args) {
	(void)argCount; (void)args;
	if (!tftEnabled || !tft) return zeroObj;
	return int2obj(tft->height);
}

static OBJ primSetPixel(int argCount, OBJ *args) {
	(void)argCount;
	tftInit();
	if (!tftEnabled || !tft) return falseObj;

	int x = obj2int(args[0]);
	int y = obj2int(args[1]);
	uint16_t c = color24_to_rgb565(obj2int(args[2]));
	tft->drawPixel(tft, x, y, c);
	return falseObj;
}

static OBJ primLine(int argCount, OBJ *args) {
	(void)argCount;
	tftInit();
	if (!tftEnabled || !tft) return falseObj;

	int x0 = obj2int(args[0]);
	int y0 = obj2int(args[1]);
	int x1 = obj2int(args[2]);
	int y1 = obj2int(args[3]);
	uint16_t c = color24_to_rgb565(obj2int(args[4]));
	tft->drawLine(tft, x0, y0, x1, y1, c);
	return falseObj;
}

static OBJ primRect(int argCount, OBJ *args) {
	tftInit();
	if (!tftEnabled || !tft) return falseObj;

	int x = obj2int(args[0]);
	int y = obj2int(args[1]);
	int w = obj2int(args[2]);
	int h = obj2int(args[3]);
	uint16_t c = color24_to_rgb565(obj2int(args[4]));
	int fill = (argCount > 5) ? (trueObj == args[5]) : true;

	if (fill) tft->fillRect(tft, x, y, w, h, c);
	else      tft->drawRect(tft, x, y, w, h, c);

	return falseObj;
}

static OBJ primCircle(int argCount, OBJ *args) {
	tftInit();
	if (!tftEnabled || !tft) return falseObj;

	int x0 = obj2int(args[0]);
	int y0 = obj2int(args[1]);
	int r  = obj2int(args[2]);
	uint16_t c = color24_to_rgb565(obj2int(args[3]));
	int fill = (argCount > 4) ? (trueObj == args[4]) : true;

	if (fill) tft->fillCircle(tft, x0, y0, r, c);
	else      tft->drawCircle(tft, x0, y0, r, c);

	return falseObj;
}

static OBJ primRoundedRect(int argCount, OBJ *args) {
	tftInit();
	if (!tftEnabled || !tft) return falseObj;

	int x = obj2int(args[0]);
	int y = obj2int(args[1]);
	int w = obj2int(args[2]);
	int h = obj2int(args[3]);
	int r = obj2int(args[4]);
	uint16_t c = color24_to_rgb565(obj2int(args[5]));
	int fill = (argCount > 6) ? (trueObj == args[6]) : true;

	// Match old behavior: clamp radius so it doesn't exceed half dims.
	if (r < 0) r = 0;
	if (2 * r >= h) r = h / 2;
	if (2 * r >= w) r = w / 2;

	if (fill) tft->fillRoundRect(tft, x, y, w, h, r, c);
	else      tft->drawRoundRect(tft, x, y, w, h, r, c);

	return falseObj;
}

static OBJ primTriangle(int argCount, OBJ *args) {
	tftInit();
	if (!tftEnabled || !tft) return falseObj;

	int x0 = obj2int(args[0]);
	int y0 = obj2int(args[1]);
	int x1 = obj2int(args[2]);
	int y1 = obj2int(args[3]);
	int x2 = obj2int(args[4]);
	int y2 = obj2int(args[5]);
	uint16_t c = color24_to_rgb565(obj2int(args[6]));
	int fill = (argCount > 7) ? (trueObj == args[7]) : true;

	// Old code always drew outline first; we’ll keep that for compatibility.
	tft->drawTriangle(tft, x0, y0, x1, y1, x2, y2, c);
	if (fill) tft->fillTriangle(tft, x0, y0, x1, y1, x2, y2, c);

	return falseObj;
}

static OBJ primText(int argCount, OBJ *args) {
	tftInit();
	if (!tftEnabled || !tft) return falseObj;

	OBJ value = args[0];
	char text[256];

	int x = obj2int(args[1]);
	int y = obj2int(args[2]);
	int color24b = obj2int(args[3]);
	int scale = (argCount > 4) ? obj2int(args[4]) : 2;
	int wrap  = (argCount > 5) ? (trueObj == args[5]) : true;

	if (IS_TYPE(value, StringType)) {
		snprintf(text, sizeof(text), "%s", obj2str(value));
	} else if (trueObj == value) {
		snprintf(text, sizeof(text), "true");
	} else if (falseObj == value) {
		snprintf(text, sizeof(text), "false");
	} else if (isInt(value)) {
		snprintf(text, sizeof(text), "%d", obj2int(value));
	} else {
		snprintf(text, sizeof(text), "");
	}

	tft->setCursor(tft, x, y);
	tft->setTextWrap(tft, wrap ? true : false);
	tft->setTextSize(tft, (scale < 1) ? 1 : scale);
	tft->setTextColorNoBG(tft, color24_to_rgb565(color24b));
	tft->print(tft, text);

	return falseObj;
}


static OBJ primClear(int argCount, OBJ *args) {
	  tftInit();
        if (!tftEnabled || !tft) return falseObj;

	tftClear();
	return falseObj;
}


// --- Simulating a 5x5 LED Matrix (same API as before) ---




void tftSetHugePixel(int x, int y, int state) {
	// simulate a 5x5 array of square pixels like the micro:bit LED array
	tftInit();
	if (!tftEnabled || !tft) return;

	int width  = tft->width;
	int height = tft->height;

	int minDimension, xInset = 0, yInset = 0;
	if (width > height) {
		minDimension = height;
		xInset = (width - height) / 2;
	} else {
		minDimension = width;
		yInset = (height - width) / 2;
	}

	int lineWidth = (minDimension > 60) ? 3 : 1;
	int squareSize = (minDimension - (6 * lineWidth)) / 5;

	uint16_t c = state ? ArduinoGFX_rgb565(0, 255, 0) : ArduinoGFX_rgb565(0, 0, 0);

	int rx = xInset + ((x - 1) * squareSize) + (x * lineWidth);
	int ry = yInset + ((y - 1) * squareSize) + (y * lineWidth);

	tft->fillRect(tft, rx, ry, squareSize, squareSize, c);
}

void tftSetHugePixelBits(int bits) {
	if (0 == bits) {
		tftClear();
	} else {
		for (int x = 1; x <= 5; x++) {
			for (int y = 1; y <= 5; y++) {
				tftSetHugePixel(x, y, bits & (1 << ((5 * (y - 1) + x) - 1)));
			}
		}
	}
}

// --- TFT Touch Primitives (no input backend here) ---

static OBJ primTftTouched(int argCount, OBJ *args) {
	(void)argCount; (void)args;
	return mouseDown ? trueObj : falseObj;
}

static OBJ primTftTouchX(int argCount, OBJ *args) {
	(void)argCount; (void)args;
	return int2obj(mouseDown ? mouseX : -1);
}

static OBJ primTftTouchY(int argCount, OBJ *args) {
	(void)argCount; (void)args;
	return int2obj(mouseDown ? mouseY : -1);
}

static OBJ primTftTouchPressure(int argCount, OBJ *args) {
	(void)argCount; (void)args;
	return int2obj(mouseDown ? (millisecs() - mouseDownTime) : -1);
}




// --- Primitives registration ---

static PrimEntry entries[] = {
	{"enableDisplay", primEnableDisplay},
	{"getWidth", primGetWidth},
	{"getHeight", primGetHeight},
	{"setPixel", primSetPixel},
	{"line", primLine},
	{"rect", primRect},
	{"roundedRect", primRoundedRect},
	{"circle", primCircle},
	{"triangle", primTriangle},
	{"text", primText},
	{"clear", primClear},
	{"tftTouched", primTftTouched},
	{"tftTouchX", primTftTouchX},
	{"tftTouchY", primTftTouchY},
	{"tftTouchPressure", primTftTouchPressure},
};

void addTFTPrims() {
	addPrimitiveSet(TFTPrims, "tft", sizeof(entries) / sizeof(PrimEntry), entries);
}
