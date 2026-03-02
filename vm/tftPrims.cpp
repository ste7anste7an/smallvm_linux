/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2018 John Maloney, Bernat Romagosa, and Jens Mönig

// tftPrims.cpp - Microblocks TFT screen primitives and touch screen input
// Bernat Romagosa, November 2018

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <stdio.h>
#include <stdlib.h>

#include "mem.h"
#include "interp.h"

int useTFT = false; // simulate 5x5 LED display on TFT display
int isOLED1106 = false;

static int hasOLED = false;
static int touchEnabled = false;
static int deferUpdates = false;

// Redefine this macro for displays that must explicitly push offscreen changes to the display
#define UPDATE_DISPLAY() { taskSleep(-1); } // yield after potentially slow TFT operations

#if defined(ARDUINO_CITILAB_ED1) || defined(ARDUINO_M5Stack_Core_ESP32) || \
	defined(ARDUINO_M5Stick_C) || defined(ARDUINO_ESP8266_WEMOS_D1MINI) || \
	defined(ARDUINO_NRF52840_CLUE) || defined(ARDUINO_IOT_BUS) || defined(SCOUT_MAKES_AZUL) || \
	defined(TTGO_RP2040) || defined(TTGO_DISPLAY) || defined(ARDUINO_M5STACK_Core2) || \
	defined(GAMEPAD_DISPLAY) || defined(PICO_ED) || defined(OLED_128_64) || defined(COCUBE) || \
	defined(M5Atom_S3_TFT) || defined(TFT_CONFIG)

	#define BLACK 0
	#define WHITE 65535

	#if defined(ARDUINO_CITILAB_ED1)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ST7735.h"

		#define TFT_CS	5
		#define TFT_DC	9
		#define TFT_RST	10
		#define TFT_WIDTH 128
		#define TFT_HEIGHT 128
		Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

		void tftInit() {
			tft.initR(INITR_144GREENTAB);
			tft.setRotation(0);
			tftClear();
			useTFT = true;
		}

	#elif defined(ARDUINO_ESP8266_WEMOS_D1MINI)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ST7735.h"

		#define TFT_CS	D4
		#define TFT_DC	D3
		#define TFT_RST	-1
		#define TFT_WIDTH 128
		#define TFT_HEIGHT 128
		Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

		void tftInit() {
			tft.initR(INITR_144GREENTAB);
			tft.setRotation(1);
			tftClear();
			useTFT = true;
		}

	#elif defined(ARDUINO_M5Stack_Core_ESP32)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ILI9341.h"
		#define TFT_CS	14
		#define TFT_DC	27
		#define TFT_RST	33
		#define TFT_WIDTH 320
		#define TFT_HEIGHT 240
		Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);
		void tftInit() {
			// test TFT_RST to see if we need to invert the display
			// (from https://github.com/m5stack/M5Stack/blob/master/src/utility/In_eSPI.cpp)
			pinMode(TFT_RST, INPUT_PULLDOWN);
			delay(1);
			bool invertFlag = digitalRead(TFT_RST);
			pinMode(TFT_RST, OUTPUT);

			tft.begin(40000000); // Run SPI at 80MHz/2
			tft.setRotation(1);
			tft.invertDisplay(invertFlag);

			uint8_t m = 0x08 | 0x04; // RGB pixel order, refresh LCD right to left
			tft.sendCommand(ILI9341_MADCTL, &m, 1);
			tftClear();
			// Turn on backlight:
			pinMode(32, OUTPUT);
			digitalWrite(32, HIGH);
			useTFT = true;
		}

	#elif defined(ARDUINO_M5Stick_C)
		// Preliminary: this is not yet working...
		#include "Adafruit_GFX.h"

		#define TFT_CS		5
		#define TFT_DC		23
		#define TFT_RST		18

		#ifdef ARDUINO_M5Stick_Plus
			#include "Adafruit_ST7789.h"
			#define TFT_WIDTH	240
			#define TFT_HEIGHT	135
		#else
			#include "Adafruit_ST7735.h"
			#define TFT_WIDTH	160
			#define TFT_HEIGHT	80
		#endif

		#ifdef ARDUINO_M5Stick_Plus
			Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
		#else
			// make a subclass so we can adjust the x/y offsets
			class M5StickLCD : public Adafruit_ST7735 {
			public:
				M5StickLCD(int8_t cs, int8_t dc, int8_t rst) : Adafruit_ST7735(cs, dc, rst) {}
				void setOffsets(int colOffset, int rowOffset) {
					_xstart = _colstart = colOffset;
					_ystart = _rowstart = rowOffset;
				}
			};
			M5StickLCD tft = M5StickLCD(TFT_CS, TFT_DC, TFT_RST);
		#endif

		int readAXP(int reg) {
			Wire1.beginTransmission(0x34);
			Wire1.write(reg);
			Wire1.endTransmission();
			Wire1.requestFrom(0x34, 1);
			return Wire1.available() ? Wire1.read() : 0;
		}

		void writeAXP(int reg, int value) {
			Wire1.beginTransmission(0x34);
			Wire1.write(reg);
			Wire1.write(value);
			Wire1.endTransmission();
		}

		void tftInit() {
			#ifdef ARDUINO_M5Stick_Plus
				tft.init(TFT_HEIGHT, TFT_WIDTH);
				tft.setRotation(3);
			#else
				tft.initR(INITR_MINI160x80);
				tft.setOffsets(26, 1);
				tft.setRotation(1);
			#endif
			tft.invertDisplay(true); // display must be inverted to give correct colors...
			tftClear();

			Wire1.begin(21, 22);
			Wire1.setClock(400000);

			// turn on LCD power pins (LD02 and LD03) = 0x0C
			// and for C+, turn on Ext (0x40) for the buzzer and DCDC1 (0x01) since M5Stack's init code does that
			int n = readAXP(0x12);
			writeAXP(0x12, n | 0x4D);

			int brightness = 12; // useful range: 7-12 (12 is max)
			n = readAXP(0x28);
			writeAXP(0x28, (brightness << 4) | (n & 0x0f)); // set brightness

			useTFT = true;
		}

	#elif defined(ARDUINO_M5STACK_Core2)
		// Preliminary: this is not yet working...
		#include "Adafruit_GFX.h"
		#include "Adafruit_ILI9341.h"
		#define TFT_CS	5
		#define TFT_DC	15
		#define TFT_WIDTH 320
		#define TFT_HEIGHT 240
		Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

		int readAXP(int reg) {
			Wire1.beginTransmission(0x34);
			Wire1.write(reg);
			Wire1.endTransmission();
			Wire1.requestFrom(0x34, 1);
			return Wire1.available() ? Wire1.read() : 0;
		}

		void writeAXP(int reg, int value) {
			Wire1.beginTransmission(0x34);
			Wire1.write(reg);
			Wire1.write(value);
			Wire1.endTransmission();
		}

		void AXP192_SetDCVoltage(uint8_t number, uint16_t voltage) {
			uint8_t addr;
			if (number > 2) return;
			voltage = (voltage < 700) ? 0 : (voltage - 700) / 25;
			switch (number) {
			case 0:
				addr = 0x26;
				break;
			case 1:
				addr = 0x25;
				break;
			case 2:
				addr = 0x27;
				break;
			}
			writeAXP(addr, (readAXP(addr) & 0x80) | (voltage & 0x7F));
		}

		void AXP192_SetLDOVoltage(uint8_t number, uint16_t voltage) {
			voltage = (voltage > 3300) ? 15 : (voltage / 100) - 18;
			if (2 == number) writeAXP(0x28, (readAXP(0x28) & 0x0F) | (voltage << 4));
			if (3 == number) writeAXP(0x28, (readAXP(0x28) & 0xF0) | voltage);
		}

		void AXP192_SetLDOEnable(uint8_t number, bool state) {
			uint8_t mark = 0x01;
			if ((number < 2) || (number > 3)) return;

			mark <<= number;
			if (state) {
				writeAXP(0x12, (readAXP(0x12) | mark));
			} else {
				writeAXP(0x12, (readAXP(0x12) & (~mark)));
			}
		}

		void AXP192_SetDCDC3(bool state) {
			uint8_t buf = readAXP(0x12);
			if (state == true) {
				buf = (1 << 1) | buf;
			} else {
				buf = ~(1 << 1) & buf;
			}
			writeAXP(0x12, buf);
		}

		void AXP192_SetLCDRSet(bool state) {
			uint8_t reg_addr = 0x96;
			uint8_t gpio_bit = 0x02;
			uint8_t data = readAXP(reg_addr);

			if (state) {
				data |= gpio_bit;
			} else {
				data &= ~gpio_bit;
			}
			writeAXP(reg_addr, data);
		}

		void AXP192_SetLed(uint8_t state) {
			uint8_t reg_addr = 0x94;
			uint8_t data = readAXP(reg_addr);

			if (state) {
				data = data & 0xFD;
			} else {
				data |= 0x02;
			}
			writeAXP(reg_addr, data);
		}

		void AXP192_SetSpkEnable(uint8_t state) {
			// Set true to enable speaker

			uint8_t reg_addr = 0x94;
			uint8_t gpio_bit = 0x04;
			uint8_t data;
			data = readAXP(reg_addr);

			if (state) {
				data |= gpio_bit;
			} else {
				data &= ~gpio_bit;
			}
			writeAXP(reg_addr, data);
		}

		void AXP192_SetCHGCurrent(uint8_t state) {
			uint8_t data = readAXP(0x33);
			data &= 0xf0;
			data = data | ( state & 0x0f );
			writeAXP(0x33, data);
		}

		void AXP192_SetBusPowerMode(uint8_t state) {
			// Select source for BUS_5V
			// 0 : powered by USB or battery; use internal boost
			// 1 : powered externally

			uint8_t data;
			if (state == 0) {
				// Set GPIO to 3.3V (LDO OUTPUT mode)
				data = readAXP(0x91);
				writeAXP(0x91, (data & 0x0F) | 0xF0);
				// Set GPIO0 to LDO OUTPUT, pullup N_VBUSEN to disable VBUS supply from BUS_5V
				data = readAXP(0x90);
				writeAXP(0x90, (data & 0xF8) | 0x02);
				// Set EXTEN to enable 5v boost
				data = readAXP(0x10);
				writeAXP(0x10, data | 0x04);
			} else {
				// Set EXTEN to disable 5v boost
				data = readAXP(0x10);
				writeAXP(0x10, data & ~0x04);
				// Set GPIO0 to float, using enternal pulldown resistor to enable VBUS supply from BUS_5V
				data = readAXP(0x90);
				writeAXP(0x90, (data & 0xF8) | 0x07);
			}
		}

		void AXP192_begin() {
			// derived from AXP192.cpp from https://github.com/m5stack/M5Core2
			Wire1.begin(21, 22);
			Wire1.setClock(400000);

			writeAXP(0x30, (readAXP(0x30) & 0x04) | 0x02); // turn vbus limit off
			writeAXP(0x92, readAXP(0x92) & 0xf8); // set gpio1 to output
			writeAXP(0x93, readAXP(0x93) & 0xf8); // set gpio2 to output
			writeAXP(0x35, (readAXP(0x35) & 0x1c) | 0xa2); // enable rtc battery charging
			AXP192_SetDCVoltage(0, 3350); // set esp32 power voltage to 3.35v
			AXP192_SetDCVoltage(2, 2800); // set backlight voltage was set to 2.8v
			AXP192_SetLDOVoltage(2, 3300); // set peripheral voltage (LCD_logic, SD card) voltage to 2.0v
			AXP192_SetLDOVoltage(3, 2000); // set vibrator motor voltage to 2.0v
			AXP192_SetLDOEnable(2, true);
			AXP192_SetDCDC3(true); // LCD backlight
			AXP192_SetLed(false);
			AXP192_SetSpkEnable(true);

			AXP192_SetCHGCurrent(0); // charge current: 100mA
			writeAXP(0x95, (readAXP(0x95) & 0x72) | 0x84); // GPIO4

			writeAXP(0x36, 0x4C); // ???
			writeAXP(0x82,0xff); // ???

			AXP192_SetLCDRSet(0);
			delay(100);
			AXP192_SetLCDRSet(1);
			delay(100);

			// axp: check v-bus status
			if (readAXP(0x00) & 0x08) {
				writeAXP(0x30, readAXP(0x30) | 0x80);
				// if has v-bus power, disable M-Bus 5V output to input
				AXP192_SetBusPowerMode(1);
			} else {
				// otherwise, enable M-Bus 5V output
				AXP192_SetBusPowerMode(0);
			}
		}

		void tftInit() {
			AXP192_begin();

			tft.begin(40000000); // Run SPI at 80MHz/2
			tft.setRotation(1);
			tft.invertDisplay(true);
			uint8_t m = 0x08 | 0x04; // RGB pixel order, refresh LCD right to left
			tft.sendCommand(ILI9341_MADCTL, &m, 1);

			tftClear();
			useTFT = true;
		}

		// M5 Core2 touchscreen support

		#define HAS_TOUCH_SCREEN 1
		#define CORE2_TOUCH_SCREEN_ADDR 0x38
		#define CORE2_SCREEN_TOUCHED_PIN 39

		static void setCore2TouchScreenReg(int regID, int value) {
			Wire1.beginTransmission(CORE2_TOUCH_SCREEN_ADDR);
			Wire1.write(regID);
			Wire1.write(value);
			Wire1.endTransmission();
		}

		static void touchInit() {
			setCore2TouchScreenReg(0xA4, 0); // hold TOUCHED_PIN low while screen touched
			pinMode(CORE2_SCREEN_TOUCHED_PIN, INPUT);
			touchEnabled = true;
		}

		static uint32 lastTouchUpdate = 0;
		static int touchScreenX = -1;
		static int touchScreenY = -1;

		static int screenTouched() {
			if (!touchEnabled) touchInit();
			return !digitalRead(CORE2_SCREEN_TOUCHED_PIN);
		}

		static void touchUpdate() {
			if (!touchEnabled) touchInit();
			uint32 now = millisecs();
			if ((now - lastTouchUpdate) < 10) return;
			if (screenTouched()) {
				uint8 data[4];
				Wire1.beginTransmission(CORE2_TOUCH_SCREEN_ADDR);
				Wire1.write(3);
				Wire1.endTransmission();
				Wire1.requestFrom(CORE2_TOUCH_SCREEN_ADDR, sizeof(data));
				for (int i = 0; i < sizeof(data); i++) {
					data[i] = Wire1.read();
				}
				touchScreenX = ((data[0] & 0xF) << 8) | data[1];
				touchScreenY = ((data[2] & 0xF) << 8) | data[3];
			} else {
				touchScreenX = -1;
				touchScreenY = -1;
			}
			lastTouchUpdate = now;
		}

		static int screenTouchX() {
			touchUpdate();
			return touchScreenX;
		}

		static int screenTouchY() {
			touchUpdate();
			return touchScreenY;
		}

		static int screenTouchPressure() {
			// pressure not supported; return a constant value if screen is touched, -1 if not
			if (!touchEnabled) touchInit();
			return screenTouched() ? 10 : -1;
		}

	#elif defined(ARDUINO_NRF52840_CLUE)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ST7789.h"

		#define TFT_CS		31
		#define TFT_DC		32
		#define TFT_RST		33
		#define TFT_WIDTH	240
		#define TFT_HEIGHT	240
		Adafruit_ST7789 tft = Adafruit_ST7789(&SPI1, TFT_CS, TFT_DC, TFT_RST);

		void tftInit() {
			tft.init(240, 240);
			tft.setRotation(1);
			tft.fillScreen(0);
			uint8_t rtna = 0x01; // Screen refresh rate control (datasheet 9.2.18, FRCTRL2)
			tft.sendCommand(0xC6, &rtna, 1);

			// fix for display gamma glitch on some Clue boards:
			uint8_t gamma = 2;
			tft.sendCommand(0x26, &gamma, 1);

			// Turn on backlight
			pinMode(34, OUTPUT);
			digitalWrite(34, HIGH);

			useTFT = true;
		}

	#elif defined(ARDUINO_IOT_BUS)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ILI9341.h"
		#include <XPT2046_Touchscreen.h>
		#include <SPI.h>

		//#define HAS_TOUCH_SCREEN 1
		#define TOUCH_CS_PIN 16
		XPT2046_Touchscreen ts(TOUCH_CS_PIN);

		#define TFT_CS	5
		#define TFT_DC	27

		#define TFT_WIDTH 320
		#define TFT_HEIGHT 240
		Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC);

		void tftInit() {
			tft.begin();
			tft.setRotation(1);
//			tft._freq = 80000000; // this requires moving _freq to public in AdaFruit_SITFT.h
			tftClear();
			// Turn on backlight on IoT-Bus
			pinMode(33, OUTPUT);
			digitalWrite(33, HIGH);

			useTFT = true;
		}

		static void touchInit() {
			ts.begin();
			ts.setRotation(1);
			touchEnabled = true;
		}

		static int screenTouched() {
			if (!touchEnabled) touchInit();
			return ts.touched();
		}

		static int screenTouchX() {
			if (!touchEnabled) touchInit();
			if (!ts.touched()) { return -1; }
			uint16_t x, y;
			uint8_t pressure;
			ts.readData(&x, &y, &pressure);
			x -= 460;
			x = (320 * x) / 3150;
			if (x < 0) x = 0;
			if (x > 320) x = 320;
			return x;
		}

		static int screenTouchY() {
			if (!touchEnabled) touchInit();
			if (!ts.touched()) { return -1; }
			uint16_t x, y;
			uint8_t pressure;
			ts.readData(&x, &y, &pressure);
			y -= 580;
			y = 240 - ((240 * y) / 2900);
			if (y < 0) y = 0;
			if (y > 240) y = 240;
			return y;
		}

		static int screenTouchPressure() {
			if (!touchEnabled) touchInit();
			if (!ts.touched()) { return -1; }
			TS_Point p = ts.getPoint();
			int pressure = (100 * (p.z - 1000)) / 2000; // pressure: 0-100
			if (pressure < 0) pressure = 0;
			if (pressure > 100) pressure = 100;
			return pressure;
		}

	#elif defined(TFT_CONFIG)
	  #if defined(TFT_ESPI)
	  	#include <TFT_eSPI.h>
		#include <XPT2046_Touchscreen.h>
		#include "touch_cst820.h"
		#include <SPI.h>
		#include "configurator.h"


		#include <LittleFS.h>
		#include <FS.h>

		inline void applyPreferred(Config& c) {
			#if defined(LMS_ESP32) && defined(ILI9341)
			useTFT = true;
			// [lcd]
			strcpy(c.lcd.controller, "ILI9341");
			c.lcd.spi = 3;
			c.lcd.mosi = 13;
			c.lcd.miso = 12;
			c.lcd.sclk = 14;
			c.lcd.cs = 15;
			c.lcd.dc = 27;
			c.lcd.rst = 32;
			c.lcd.rotation = 3;
			c.lcd.color = 1;
			c.lcd.backlight = 33;
			c.lcd.width = 320;
			c.lcd.height = 240;

			// [lvgl]
			c.lvgl.width = 320;
			c.lvgl.height = 240;

			// [touch]
			strcpy(c.touch.interface, "spi");
			strcpy(c.touch.controller, "xpt2046");
			c.touch.spi = 3;
			c.touch.mosi = 13;
			c.touch.miso = 12;
			c.touch.sclk = 14;
			c.touch.cs = 26;
			c.touch.rotation = 3;
		
    // [other]
		
			#else
				// empty cfg
				 (void)c;
			#endif
		}

		
		Config cfg = {};              // zero-init (PIN_UNUSED, false, "")
    	

		TFT_eSPI tft = TFT_eSPI();
	  	XPT2046_Touchscreen *touch = nullptr;
	//	SPIClass* tftSPI = nullptr;
		SPIClass* touchSPI = nullptr;
		SPIClass& spix = SPI;

		#define HAS_TOUCH_SCREEN 1

		// New (I2C/CST820):
		static TouchCST820 *touchI2C = nullptr;

		// Cache last point for XPT so we don’t call getPoint() multiple times per frame
		static TS_Point lastP;
		static bool lastPTouched = false;
		static uint32_t lastTouchPoll = 0;
	
	

		void tftInit() {
			//Serial.println("starting tftinit\r\n");

			useTFT = true;
			#ifndef TFT_WIDTH
			#define TFT_WIDTH  (cfg.lvgl.width)
			#endif

			#ifndef TFT_HEIGHT
			#define TFT_HEIGHT (cfg.lvgl.height)
			#endif
			char s[100];
			bool config_file_exists=false;
			        // pure zero-initialization
			// configurator::loadConfig(&cfg);
			// configurator::setDefaults(&cfg);
			
			if (!LittleFS.begin()) {
					sprintf(s,"LittleFS mount failed!\n");
					outputString(s);
					//return;
				}

			if (!LittleFS.exists("/config.txt")) {
				//Serial.printf("File does not exist!\n\r");
				useTFT = false;
				applyPreferred(cfg);  		
			} else {
				config_file_exists=true;
				cfg={};
			}  

			if (config_file_exists) {
				if (!configurator::loadConfig(&cfg)) {
						sprintf(s,"Defaults used");
						
						outputString(s);
				}
			}

			/*
			 Serial.printf("non configured: touch.i2c %d, touch.scl:%d touch:sda %d,  cfg.lcd.invert %d,cfg.touch.flip_x %d,cfg.touch.flip_y %d,cfg.touch.flip_x_y %d\r\n",
			 	 cfg.touch.i2c,toGPIO(cfg.touch.scl),toGPIO(cfg.touch.scl),cfg.lcd.invert,cfg.touch.flip_x,cfg.touch.flip_y,cfg.touch.flip_x_y);

			 Serial.printf("cfg.lcd.controller %s , cfg.touch.interface %s, cfg.touch.controller %s\r\n", cfg.lcd.controller,cfg.touch.interface,cfg.touch.controller);

			 Serial.printf("toGPIO(cfg.lcd.dc) %d, toGPIO(cfg.lcd.cs) %d,toGPIO(cfg.lcd.sclk) %d, toGPIO(cfg.lcd.mosi) %d, toGPIO(cfg.lcd.miso) %d, cfg.lcd.spi %d \r\n",
					toGPIO(cfg.lcd.dc), toGPIO(cfg.lcd.cs),	toGPIO(cfg.lcd.sclk), toGPIO(cfg.lcd.mosi), toGPIO(cfg.lcd.miso),
						cfg.lcd.spi);
			 Serial.printf(" toGPIO(cfg.lcd.rst) %d, cfg.lcd.rotation %d, cfg.lcd.invert %d,cfg.lcd.width %d, cfg.lcd.height %d,cfg.lcd.col_offset %d,cfg.lcd.row_offset %d\r\n",
				 toGPIO(cfg.lcd.rst), cfg.lcd.rotation, cfg.lcd.invert,cfg.lcd.width, cfg.lcd.height,cfg.lcd.col_offset,cfg.lcd.row_offset);
			*/
		
			tft.begin();
			tft.init();
			tft.initDMA();
			//tft.setSwapBytes(true);
			spix = tft.getSPIinstance(); 
			//tft.fillScreen(TFT_BLACK);
		
			tft.begin();
			tft.setRotation(cfg.lcd.rotation);
	//			tft._freq = 80000000; // this requires moving _freq to public in AdaFruit_SITFT.h
			tftClear();
			// Turn on backlight on IoT-Bus
			tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 20);
  tft.println("LovyanGFX OK");

			delay(1); 
			useTFT = true;
			//Serial.printf("backlight %d\r\n",cfg.lcd.backlight);
			pinMode(toGPIO(cfg.lcd.backlight), OUTPUT);
			digitalWrite(toGPIO(cfg.lcd.backlight), HIGH); // turn backlight ON (or LOW if your display is inverted)
			//Serial.printf("TFT completely initilaized\r\n");

		}

	  #else
		#include <Arduino.h>
		#include <Arduino_GFX_Library.h>
		#include <XPT2046_Touchscreen.h>
		#include "touch_cst820.h"
		#include <SPI.h>
		#include "configurator.h"


		#include <LittleFS.h>
		#include <FS.h>

		#define ARDUINO_GFX_USE_PSRAM

		inline void applyPreferred(Config& c) {
			#if defined(LMS_ESP32) && defined(ST7789)
			useTFT = true;
			
			// [lcd]
			strcpy(c.lcd.controller, "ST7789");
			c.lcd.spi = 3;
			c.lcd.mosi = 26;
			c.lcd.sclk = 15;
			c.lcd.cs = 14;
			c.lcd.dc = 27;
			c.lcd.rst = 13;
			c.lcd.rotation = 1;
			c.lcd.invert = true;
			c.lcd.backlight = 12;
			c.lcd.width = 240;
			c.lcd.height = 280;
			c.lcd.row_offset = 20;

			// [lvgl]
			c.lvgl.width = 280;
			c.lvgl.height = 240;

			// [touch]
			strcpy(c.touch.controller, "cst820");
			strcpy(c.touch.interface, "i2c");
			c.touch.i2c = 1;
			c.touch.sda = 32;
			c.touch.scl = 33;
			c.touch.rotation = 0;
			c.touch.flip_y = true;
			#elif defined(S3_ELECROW) 
			Serial.printf("configured S3_ELECROW\r\n");
			useTFT = true;
			
			// [lcd]
			strcpy(c.lcd.controller, "ELECROW");
			c.lcd.backlight = 2;
			c.lcd.width = 480;
			c.lcd.height = 277;
			
			// [lvgl]
			c.lvgl.width = 480;
			c.lvgl.height = 277;

			// [touch]
			strcpy(c.touch.controller, "xpt2046");
			strcpy(c.touch.interface, "spi");
			c.touch.spi = 1;
			c.touch.miso = 13;
			c.touch.mosi = 11;
			c.touch.sclk = 12;
			c.touch.cs = PIN_ZERO;
			c.touch.irq=36;
			c.touch.rotation = 0;
			// [other]
			#else
				// empty cfg
				 (void)c;
			#endif
		}

		
		Config cfg = {};              // zero-init (PIN_UNUSED, false, "")
    	

		//static Arduino_DataBus *bus = nullptr;
		static Arduino_DataBus *bus=nullptr;
		// static Arduino_ESP32RGBPanel *rgbpanel =  new Arduino_ESP32RGBPanel(
		// 		40 /* DE */, 41 /* VSYNC */, 39 /* HSYNC */, 42 /* PCLK */,
		// 		45 /* R0 */, 48 /* R1 */, 47 /* R2 */, 21 /* R3 */, 14 /* R4 */,
		// 		5 /* G0 */, 6 /* G1 */, 7 /* G2 */, 15 /* G3 */, 16 /* G4 */, 4 /* G5 */,
		// 		8 /* B0 */, 3 /* B1 */, 46 /* B2 */, 9 /* B3 */, 1 /* B4 */,
		// 		0 /* hsync_polarity */, 8 /* hsync_front_porch */, 4 /* hsync_pulse_width */, 43 /* hsync_back_porch */,
		// 		0 /* vsync_polarity */, 8 /* vsync_front_porch */, 4 /* vsync_pulse_width */, 12 /* vsync_back_porch */,
		// 		1 /* pclk_active_neg */, 7000000 /* prefer_speed */);

    	// Arduino_RGB_Display tft = Arduino_RGB_Display(
		//  		 480 /* width */, 272 /* height */, rgbpanel, 0 /* rotation */, true /* auto_flush */);

		static Arduino_GFX *gfx = nullptr;
		//static Arduino_RGB_Display *rgb_tft = nullptr; 
		//Arduino_RGB_Display *tft2=nullptr;
		//Arduino_RGB_Display tft
		#define tft (*gfx)
		//Arduino_GFX& tft = *gfx;
		XPT2046_Touchscreen *touch = nullptr;
		SPIClass* tftSPI = nullptr;
		SPIClass* touchSPI = nullptr;


		#define HAS_TOUCH_SCREEN 1

		// New (I2C/CST820):
		static TouchCST820 *touchI2C = nullptr;

		// Cache last point for XPT so we don’t call getPoint() multiple times per frame
		static TS_Point lastP;
		static bool lastPTouched = false;
		static uint32_t lastTouchPoll = 0;
	
	

		void tftInit() {
			//Serial.println("starting tftinit\r\n");

			useTFT = false;
			#ifndef TFT_WIDTH
			#define TFT_WIDTH  (cfg.lvgl.width)
			#endif

			#ifndef TFT_HEIGHT
			#define TFT_HEIGHT (cfg.lvgl.height)
			#endif
			char s[100];
			bool config_file_exists=false;
			        // pure zero-initialization
			// configurator::loadConfig(&cfg);
			// configurator::setDefaults(&cfg);
			
			if (!LittleFS.begin()) {
					sprintf(s,"LittleFS mount failed!\n");
					outputString(s);
					//return;
				}

			if (!LittleFS.exists("/config.txt")) {
				//Serial.printf("File does not exist!\n\r");
				useTFT = false;
				applyPreferred(cfg);  		
			} else {
				config_file_exists=true;
				cfg={};
			}  
			/*
			 Serial.printf("non configured: touch.i2c %d, touch.scl:%d touch:sda %d,  cfg.lcd.invert %d,cfg.touch.flip_x %d,cfg.touch.flip_y %d,cfg.touch.flip_x_y %d\r\n",
			 	 cfg.touch.i2c,toGPIO(cfg.touch.scl),toGPIO(cfg.touch.scl),cfg.lcd.invert,cfg.touch.flip_x,cfg.touch.flip_y,cfg.touch.flip_x_y);

			 Serial.printf("cfg.lcd.controller %s , cfg.touch.interface %s, cfg.touch.controller %s\r\n", cfg.lcd.controller,cfg.touch.interface,cfg.touch.controller);

			 Serial.printf("toGPIO(cfg.lcd.dc) %d, toGPIO(cfg.lcd.cs) %d,toGPIO(cfg.lcd.sclk) %d, toGPIO(cfg.lcd.mosi) %d, toGPIO(cfg.lcd.miso) %d, cfg.lcd.spi %d \r\n",
					toGPIO(cfg.lcd.dc), toGPIO(cfg.lcd.cs),	toGPIO(cfg.lcd.sclk), toGPIO(cfg.lcd.mosi), toGPIO(cfg.lcd.miso),
						cfg.lcd.spi);
			 Serial.printf(" toGPIO(cfg.lcd.rst) %d, cfg.lcd.rotation %d, cfg.lcd.invert %d,cfg.lcd.width %d, cfg.lcd.height %d,cfg.lcd.col_offset %d,cfg.lcd.row_offset %d\r\n",
				 toGPIO(cfg.lcd.rst), cfg.lcd.rotation, cfg.lcd.invert,cfg.lcd.width, cfg.lcd.height,cfg.lcd.col_offset,cfg.lcd.row_offset);
			*/
			if (config_file_exists) {
				if (!configurator::loadConfig(&cfg)) {
						sprintf(s,"Defaults used");
						
						outputString(s);
				}
			}

	
					
		// } else {
			if (cfg.lcd.dc != PIN_UNUSED && cfg.lcd.cs != PIN_UNUSED && cfg.lcd.sclk != PIN_UNUSED && cfg.lcd.mosi != PIN_UNUSED && cfg.lcd.spi>=0 &&
				 strlen(cfg.lcd.controller)>0)
			 {
				// tftSPI = new SPIClass(cfg.lcd.spi);
				// tftSPI->begin(toGPIO(cfg.lcd.sclk), toGPIO(cfg.lcd.miso), toGPIO(cfg.lcd.mosi), toGPIO(cfg.lcd.cs));
				// bus = new Arduino_ESP32SPI(cfg.lcd.spi, toGPIO(cfg.lcd.dc), toGPIO(cfg.lcd.cs));
				// 	//bus->begin(10000000,SPI_MODE0);
#if defined(ESP32_C3)
				bus = new Arduino_ESP32SPI( toGPIO(cfg.lcd.dc), toGPIO(cfg.lcd.cs), toGPIO(cfg.lcd.sclk), toGPIO(cfg.lcd.mosi), 
											toGPIO(cfg.lcd.miso) );
											// last boolean is: shared spi interface
#else
				bus = new Arduino_ESP32SPI( toGPIO(cfg.lcd.dc), toGPIO(cfg.lcd.cs), toGPIO(cfg.lcd.sclk), toGPIO(cfg.lcd.mosi), 
											toGPIO(cfg.lcd.miso), cfg.lcd.spi, (cfg.lcd.spi==cfg.touch.spi));
											// last boolean is: shared spi interface
				
#endif							


		
				if (strcmp(cfg.lcd.controller, "ILI9341") == 0) {
					gfx = new Arduino_ILI9341(bus, toGPIO(cfg.lcd.rst), cfg.lcd.rotation, cfg.lcd.invert);
				} else if (strcmp(cfg.lcd.controller, "ST7789") == 0) {
					//Serial.printf("ST7789 controller configured\r\n");
					gfx = new Arduino_ST7789(bus, toGPIO(cfg.lcd.rst), cfg.lcd.rotation, cfg.lcd.invert,cfg.lcd.width, cfg.lcd.height,cfg.lcd.col_offset,cfg.lcd.row_offset);
				}  else if (strcmp(cfg.lcd.controller, "ST7796") == 0) {
					//if (cfg.lcd.col_offset==0 && cfg.lcd.row_offset==0)
					//  gfx = new Arduino_ST7796(bus, cfg.lcd.rst, cfg.lcd.rotation, false);
					//else
					gfx = new Arduino_ST7796(bus, toGPIO(cfg.lcd.rst), cfg.lcd.rotation, cfg.lcd.invert,cfg.lcd.width, cfg.lcd.height,cfg.lcd.col_offset,cfg.lcd.row_offset);
				} else if (strcmp(cfg.lcd.controller, "GC9A01") == 0) {
					//if (cfg.lcd.col_offset==0 && cfg.lcd.row_offset==0)
					//  gfx = new Arduino_ST7796(bus, cfg.lcd.rst, cfg.lcd.rotation, false);
					//else
					gfx = new Arduino_GC9A01(bus, toGPIO(cfg.lcd.rst), cfg.lcd.rotation, cfg.lcd.invert,cfg.lcd.width, cfg.lcd.height,cfg.lcd.col_offset,cfg.lcd.row_offset);
				}
				//else {
				//	Serial.println("Unknown controller\r\n");
				//}
				
				if (gfx != nullptr) {
					//Serial.printf("tft.begin()\r\n");
					delay(100); 
					tft.begin();
					tft.fillScreen(RGB565_BLACK);
					delay(1); 
					useTFT = true;
					//Serial.printf("backlight %d\r\n",cfg.lcd.backlight);
					pinMode(toGPIO(cfg.lcd.backlight), OUTPUT);
					digitalWrite(toGPIO(cfg.lcd.backlight), HIGH); // turn backlight ON (or LOW if your display is inverted)
					//Serial.printf("TFT completely initilaized\r\n");
				}
			} //else Serial.printf("No TFT used\r\n");
		}
		#endif
	
			// --- State flags ---
		static bool touchInitAttempted = false;
		static bool hasTouch = false;

		// --- Touch type helpers ---
		static inline bool isTouchXPT(void) {
			return (0 == strcmp(cfg.touch.interface, "spi")) &&
				(0 == strcmp(cfg.touch.controller, "xpt2046"));
		}

		static inline bool isTouchCST(void) {
			return (0 == strcmp(cfg.touch.interface, "i2c")) &&
				(0 == strcmp(cfg.touch.controller, "cst820"));
		}

		// --- Initialization ---
		static void touchInit() {
			//Serial.printf("touchinit entered\r\n");
			if (touchInitAttempted) return;  // prevent endless retries
			touchInitAttempted = true;

			if (touchEnabled) return;

			if (isTouchXPT()) {
				#if !defined(ILI9341)
					touchSPI = new SPIClass(cfg.touch.spi);
					touchSPI->begin(toGPIO(cfg.touch.sclk), toGPIO(cfg.touch.miso), toGPIO(cfg.touch.mosi), toGPIO(cfg.touch.cs));
				#endif
				touchEnabled = true;
				hasTouch = true;

				if (cfg.touch.irq != PIN_UNUSED)
					touch = new XPT2046_Touchscreen(toGPIO(cfg.touch.cs), toGPIO(cfg.touch.irq));
				else
					touch = new XPT2046_Touchscreen(toGPIO(cfg.touch.cs));
				#if defined(ILI9341)
					touch->begin(spix);
				#else
					touch->begin(*touchSPI);
				#endif
				touch->setRotation(cfg.touch.rotation);
				return;
			}

			if (isTouchCST()) {
				// Serial.printf("initialize CST820   sda=%d scl=%d\r\n",toGPIO(cfg.touch.sda), toGPIO(cfg.touch.scl));
				touchI2C = new TouchCST820();
				touchI2C->configure(cfg.touch.i2c, toGPIO(cfg.touch.sda), toGPIO(cfg.touch.scl));
				touchI2C->setScreenSize(cfg.lvgl.width, cfg.lvgl.height);
				touchI2C->begin();
				hasTouch = true;
				touchEnabled = true;
				return;
			}
			
			touchEnabled = false;
			hasTouch = false;
		}

		// --- Helper to check readiness ---
		static inline bool touchReady() {
			if (!touchEnabled && !touchInitAttempted)
				touchInit();
			return touchEnabled;
		}

		// --- Touch read functions ---
		static int screenTouched() {
			//Serial.printf("screentouched entered\r\n");
			if (!touchReady()) return 0;

			if (isTouchXPT()) {
				uint32_t now = millis();
				if ((uint32_t)(now - lastTouchPoll) > 5) {
					lastPTouched = (touch && touch->touched());
					if (lastPTouched) lastP = touch->getPoint();
					lastTouchPoll = now;
				}
				return lastPTouched ? 1 : 0;
			}

			if (isTouchCST()) {
				return (touchI2C && touchI2C->touched()) ? 1 : 0;
			}

			return 0;
		}

		static int screenTouchX() {
			//Serial.printf("screentouchX entered\r\n");
			if (!touchReady()) return 0;
			int16_t x;

			if (isTouchXPT()) {
				if (!screenTouched()) return 0;
				if (cfg.touch.flip_x_y)
					x = (int)lastP.y;
				else
					x = (int)lastP.x;

				if (cfg.touch.flip_x)
					return (int)map((int)lastP.x, 200, 3800, 0, (int)cfg.lvgl.width);
				else
					return (int)map((int)lastP.x, 200, 3800, (int)cfg.lvgl.width, 0);
			}

			if (isTouchCST()) {
				if (!touchI2C) return 0;
				if (cfg.touch.flip_x_y)
					x = touchI2C->y();
				else
					x = touchI2C->x();

				if (cfg.touch.flip_x)
					return cfg.lvgl.width - x;
				else
					return x;
			}

			return 0;
		}

		static int screenTouchY() {
			//Serial.printf("screentouchY entered\r\n");
			if (!touchReady()) return 0;
			int16_t y;

			if (isTouchXPT()) {
				if (!screenTouched()) return 0;
				if (cfg.touch.flip_x_y)
					y = (int)lastP.x;
				else
					y = (int)lastP.y;

				if (cfg.touch.flip_y)
					return (int)map((int)lastP.y, 300, 3900, (int)cfg.lvgl.height, 0);
				else
					return (int)map((int)lastP.y, 300, 3900, 0, (int)cfg.lvgl.height);
			}

			if (isTouchCST()) {
				if (!touchI2C) return 0;
				if (cfg.touch.flip_x_y)
					y = touchI2C->x();
				else
					y = touchI2C->y();

				if (cfg.touch.flip_y)
					return cfg.lvgl.height - y;
				else
					return y;
			}

			return 0;
		}

		static int screenTouchPressure() {
			if (!touchReady()) return 0;

			if (isTouchXPT()) {
				if (!screenTouched()) return 0;
				return (int)lastP.z;
			}

			if (isTouchCST()) {
				if (!touchI2C) return 0;
				return touchI2C->pressure();  // 1000 when touched, -1 otherwise
			}

			return 0;
		}

		static int screenTouchGesture() {
			if (!touchReady()) return 0;

			if (isTouchXPT()) {
				return 0;  // no gesture support
			}

			if (isTouchCST()) {
				return touchI2C ? touchI2C->gesture() : 0;
			}

			return 0;
		}


	#elif defined(SCOUT_MAKES_AZUL)
		#undef BLACK // defined in SSD1306 header
		#include "Adafruit_GFX.h"
		#include "Adafruit_SSD1306.h"

		#define TFT_WIDTH 128
		#define TFT_HEIGHT 32
		#define IS_MONOCHROME true

		Adafruit_SSD1306 tft = Adafruit_SSD1306(TFT_WIDTH, TFT_HEIGHT);

		#undef UPDATE_DISPLAY
		#define UPDATE_DISPLAY() { if (!deferUpdates) { tft.display(); taskSleep(-1); }}

		void tftInit() {
			tft.begin(SSD1306_SWITCHCAPVCC, 0x3C);
			useTFT = true;
			tftClear();
		}

	#elif defined(OLED_128_64)
		#undef BLACK // defined in SSD1306 header
		#undef WHITE // defined in SSD1306 header
		#include "Adafruit_GFX.h"
		#include "Adafruit_SSD1306.h"

		#define TFT_ADDR 0x3C
		#define TFT_WIDTH 128
		#define TFT_HEIGHT 64
		#define IS_MONOCHROME true

		Adafruit_SSD1306 tft = Adafruit_SSD1306(TFT_WIDTH, TFT_HEIGHT, &Wire, -1, 400000, 400000);

		static void oledCmd(uint8 cmd) {
			Wire.beginTransmission(TFT_ADDR);
			Wire.write(0x80);
			Wire.write(cmd);
			Wire.endTransmission(true);
		}

		void tftInit() {
			delay(5); // need 2 msecs minimum for micro:bit PicoBricks board power up I2C pullups
			if (!hasI2CPullups()) return; // no OLED connected and no I2C pullups

			// Ping the DUELink address so DUELink modules will use I2C mode
			// (This must be the first I2C transaction after power up.)
			readI2CReg(82, 0);

			int response = readI2CReg(TFT_ADDR, 0); // test if OLED responds at TFT_ADDR
			if (response < 0) return; // no OLED display detected
			isOLED1106 = (8 == (response & 15));

			tft.begin(SSD1306_SWITCHCAPVCC, TFT_ADDR);

			// set to medium brightness
			oledCmd(0x81);
			oledCmd(0x80);

			hasOLED = true;
			#if defined(KIDS_BITS)
				useTFT = true; // simulate TFT on KidsBits OLED display
			#endif
			tftClear();
		}

		static void i2cWriteBytes(uint8 *bytes, int byteCount) {
			Wire.beginTransmission(TFT_ADDR);
			for (int i = 0; i < byteCount; i++) Wire.write(bytes[i]);
			Wire.endTransmission(true);
		}

		static void oledUpdate() {
			// Send the entire OLED buffer to the display via i2c. Takes about 30 msecs.
			// Periodically update the LED display to avoid flicker.
			uint8 setupCmds[] = {
				0x20, 0,		// Horizontal mode
				0x21, 0, 0x7F,	// Column start and end address
				0x22, 0, 7		// Page start and end address
			};
			i2cWriteBytes(setupCmds, sizeof(setupCmds));
			uint8 buffer[65];
			buffer[0] = 0x40;
			uint8 *src = tft.getBuffer();
			for (int i = 0; i < 8; i++) {
				// do time-sensitive background tasks
				captureIncomingBytes();
				updateMicrobitDisplay();

				oledCmd(0x10);
				oledCmd(isOLED1106 ? 0x02 : 0); // column offset
				oledCmd(0xB0 + i);

				// write 128 bytes of data in two i2c writes
				memcpy(&buffer[1], src, 64);
				i2cWriteBytes(buffer, 65);
				src += 64;
				memcpy(&buffer[1], src, 64);
				i2cWriteBytes(buffer, 65);
				src += 64;
			}
		}

		#undef UPDATE_DISPLAY
		#define UPDATE_DISPLAY() { if (!deferUpdates) { oledUpdate(); taskSleep(-1); }}

	#elif defined(TTGO_DISPLAY)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ST7789.h"

		#define TFT_MOSI 19
		#define TFT_SCLK 18
		#define TFT_CS 5
		#define TFT_DC 16
		#define TFT_RST 23
		#define TFT_BL 4
		#define TFT_WIDTH 240
		#define TFT_HEIGHT 135
		#define TFT_PWR 22
		Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

		void tftInit() {
			pinMode(TFT_BL, OUTPUT);
			digitalWrite(TFT_BL, 1);
			tft.init(TFT_HEIGHT, TFT_WIDTH);
			tft.setRotation(1);
			tftClear();
			useTFT = true;
		}

	#elif defined(GAMEPAD_DISPLAY)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ST7735.h"

		#define TFT_MOSI 13
		#define TFT_SCLK 14
		#define TFT_CS 18
		#define TFT_DC 16
		#define TFT_RST 17
		#define TFT_WIDTH 128
		#define TFT_HEIGHT 128
		Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

		void tftInit() {
			tft.initR(INITR_144GREENTAB);
			tft.setRotation(3);
			tft.fillScreen(BLACK);
			useTFT = true;
		}

	#elif defined(TTGO_RP2040)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ST7789.h"

		#define TFT_MOSI 3
		#define TFT_SCLK 2
		#define TFT_CS 5
		#define TFT_DC 1
		#define TFT_RST 0
		#define TFT_BL 4
		#define TFT_WIDTH 240
		#define TFT_HEIGHT 135
		#define TFT_PWR 22
		Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

		void tftInit() {
			pinMode(TFT_PWR, OUTPUT);
			pinMode(TFT_BL, OUTPUT);
			digitalWrite(TFT_PWR, 1);
			tft.init(TFT_HEIGHT, TFT_WIDTH);
			analogWrite(TFT_BL, 250);
			tft.setRotation(1);
			tftClear();
			useTFT = true;
		}

	#elif defined(PICO_ED)
		#include <Adafruit_GFX.h>

		#define TFT_WIDTH 17
		#define TFT_HEIGHT 7
		#define IS_GRAYSCALE true

		// IS31FL3731 constants
		#define IS31FL_ADDR 0x74
		#define IS31FL_BANK_SELECT 0xFD
		#define IS31FL_FUNCTION_BANK 0x0B
		#define IS31FL_SHUTDOWN_REG 0x0A
		#define IS31FL_CONFIG_REG 0x00
		#define IS31FL_PICTUREFRAME_REG 0x01

		#undef UPDATE_DISPLAY
		#define UPDATE_DISPLAY() { if (!deferUpdates) tft.updateDisplay(); }

		class IS31FL3731 : public Adafruit_GFX {
		public:
			IS31FL3731(uint8_t width, uint8_t height) : Adafruit_GFX(width, height) {}

			TwoWire *wire;
			uint8 displayBuffer[144];

			bool begin();
			void drawPixel(int16_t x, int16_t y, uint16_t brightness);
			void clearDisplayBuffer();
			void showMicroBitPixels(int microBitDisplayBits, int xPos, int yPos);
			void updateDisplay(void);
			void setRegister(uint8_t reg, uint8_t value);
		};

		bool IS31FL3731::begin() {
			wire = &Wire1;
			if (readI2CReg(IS31FL_ADDR, 0) < 0) {
				// no display on external i2c bus, so this is a pico:ed v2

				// initialize internal i2c bus
				wire = &Wire;
				wire->setSDA(0);
				wire->setSCL(1);
				wire->begin();
				wire->setClock(400000);

				// speaker in on pin 3 of pico:ed v2
				setPicoEdSpeakerPin(3);
			}

			// select the function bank
			setRegister(IS31FL_BANK_SELECT, IS31FL_FUNCTION_BANK);

			// toggle shutdown
			setRegister(IS31FL_SHUTDOWN_REG, 0);
			delay(10);
			setRegister(IS31FL_SHUTDOWN_REG, 1);

			// picture mode
			setRegister(IS31FL_CONFIG_REG, 0);

			// set frame to display
			setRegister(IS31FL_PICTUREFRAME_REG, 0);

			// clear the display before enabling LED's
			memset(displayBuffer, 0, sizeof(displayBuffer));
			updateDisplay();

			// enable all LEDs
			for (uint8_t bank = 0; bank < 8; bank++) {
				setRegister(IS31FL_BANK_SELECT, bank);
				for (uint8_t i = 0; i < 18; i++) {
					setRegister(i, 0xFF);
				}
			}
			return true;
		}

		void IS31FL3731::clearDisplayBuffer() {
			memset(displayBuffer, 0, sizeof(displayBuffer));
		}

		void IS31FL3731::drawPixel(int16_t x, int16_t y, uint16_t brightness) {
			// Set the brightness of the pixel at (x, y).

			const uint8_t topRow[17] =
				{7, 23, 39, 55, 71, 87, 103, 119, 135, 136, 120, 104, 88, 72, 56, 40, 24};

			if ((x < 0) || (x > 16)) return;
			if ((y < 0) || (y > 6)) return;

			// adjust brightness (use range 0-100 to avoid making LED's painfully bright)
			if ((brightness != 0) && (brightness < 3)) brightness = 3; //
			brightness = (100 * brightness) / 255;
			if (brightness > 100) brightness = 100;

			int incr = (x < 9) ? -1 : 1;
			int i = topRow[x] + (y * incr);
			displayBuffer[i] = brightness;
		}

		void IS31FL3731::showMicroBitPixels(int microBitDisplayBits, int xPos, int yPos) {
			// Draw 5x5 image at the given location where 1,1 is the origin.

			int brightness = 100;
			int y = yPos;
			for (int i = 0; i < 25; i++) {
				int x = (i % 5) + 5 + xPos;
				if ((5 < x) && (x < 11) && (0 < y) && (y < 6)) {
					if (microBitDisplayBits & (1 << i)) drawPixel(x, y, brightness);
				}
				if ((i % 5) == 4) y++;
			}
			updateDisplay();
		}

		void IS31FL3731::updateDisplay() {
			// Write the entire display buffer to bank 0.

			setRegister(IS31FL_BANK_SELECT, 0); // select bank 0
			for (uint8_t i = 0; i < 6; i++) {
				wire->beginTransmission(IS31FL_ADDR);
				wire->write(0x24 + (24 * i)); // offset within bank
				wire->write(&displayBuffer[24 * i], 24);
				wire->endTransmission();
			}
		}

		void IS31FL3731::setRegister(uint8_t reg, uint8_t value) {
			wire->beginTransmission(IS31FL_ADDR);
			wire->write(reg);
			wire->write(value);
			wire->endTransmission();
		}

		// pretend display is 7 pixels wider so GFX will display partial characters
		IS31FL3731 tft = IS31FL3731(TFT_WIDTH + 7, TFT_HEIGHT);

		void tftInit() {
			tft.begin();
			useTFT = true;
		}

	void showMicroBitPixels(int microBitDisplayBits, int xPos, int yPos) {
		// Used by scrolling text; don't clear display.
		tft.showMicroBitPixels(microBitDisplayBits, xPos, yPos);
	}

	#elif defined(COCUBE)
		#include "Adafruit_GFX.h"
		#include "Adafruit_ST7789.h"
		#include <LittleFS.h>

		#define TFT_MOSI 19
		#define TFT_SCLK 27
		#define TFT_CS -1
		#define TFT_DC 32
		#define TFT_RST 2
		#define TFT_BL 33
		#define TFT_WIDTH 240
		#define TFT_HEIGHT 240
		#define DEFAULT_BATTERY_PIN 34
		#define LOGO_PATH "/logo.raw"

		SPIClass CoCubeSPI(VSPI);
		Adafruit_ST7789 tft = Adafruit_ST7789(&CoCubeSPI, TFT_CS, TFT_DC, TFT_RST);

		void drawRawImage(const char* filename, int x0, int y0, int width, int height) {
			if (!LittleFS.begin()) return;

			File file = LittleFS.open(filename, "r");
			if (!file) return;

			uint16_t lineBuf[width];
			for (int y = 0; y < height; y++) {
				size_t read = file.read((uint8_t*)lineBuf, width * 2);
				if (read != width * 2) break;
				tft.drawRGBBitmap(x0, y0 + y, lineBuf, width, 1);
			}
			file.close();
		}

		void drawBatteryStatus(int percentage, int x, int y, int width, int height, int textSize) {
			uint16_t fillColor = 0x07E0; // GREEN
			if (percentage < 67) fillColor = 0xFD20; // YELLOW
			if (percentage < 34) fillColor = 0xF800; // RED

			uint16_t borderColor = 0x0000; // BLACK
			uint16_t textColor = 0x0000;   // BLACK by default

			int level = map(percentage, 0, 100, 0, width - 4);
			tft.drawRoundRect(x, y, width, height, 3, borderColor);
			int headW = width / 10;
			tft.fillRect(x + width, y + height / 4, headW, height / 2, 0x4208);
			tft.fillRect(x + 2, y + 2, level, height - 4, fillColor);

			char buf[6];
			sprintf(buf, "%d%%", percentage);

			int charW = 6 * textSize;
			int charH = 8 * textSize;
			int textLen = strlen(buf);

			int textX = x + (width - textLen * charW) / 2;
			int textY = y + (height - charH) / 2;

			tft.setTextColor(textColor);
			tft.setTextSize(textSize);
			tft.setCursor(textX, textY);
			tft.print(buf);
		}

		void tftInit() {
			CoCubeSPI.begin(TFT_SCLK, -1, TFT_MOSI, -1);
			tft.init(TFT_HEIGHT, TFT_WIDTH, SPI_MODE3);
			tft.setRotation(1);
			pinMode(TFT_BL, OUTPUT);
			digitalWrite(TFT_BL, HIGH);
			useTFT = true;

			int batteryRaw = 0;
			for (int i = 0; i < 5; i++) {
				batteryRaw += analogRead(DEFAULT_BATTERY_PIN);
				delay(5);
			}
			int battery_percentage = constrain(((44 * batteryRaw / 105.0 - 6800) / 16.0), 0, 99);

			bool logoDisplayed = false;
			LittleFS.begin();
			File logo = LittleFS.open(LOGO_PATH, "r");
			if (logo) {
				logo.close();
				drawRawImage(LOGO_PATH, 0, 0, 240, 240);
				drawBatteryStatus(battery_percentage, 85, 150, 70, 40, 3);
				logoDisplayed = true;
				delay(1000);
			}

			if (!logoDisplayed) {
				tft.fillScreen(WHITE);
				drawBatteryStatus(battery_percentage, 85, 100, 70, 40, 3);
				delay(1000);
			}
			tft.fillScreen(BLACK);
		}

	#elif defined(M5Atom_S3_TFT)
		#undef BLACK // defined GFX
		#undef WHITE // defined GFX
		#define drawRGBBitmap draw16bitRGBBitmap
		#include <Arduino_GFX_Library.h>
		#define TFT_MOSI 21
		#define TFT_SCLK 17
		#define TFT_CS   15
		#define TFT_DC   33
		#define TFT_RST  34
		#define TFT_BL   16
		#define TFT_WIDTH 128
		#define TFT_HEIGHT 128
		Arduino_ESP32SPI bus = Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, -1);
		Arduino_GC9107 tft = Arduino_GC9107(&bus, TFT_RST, 0 /* rotation */, true /* IPS */);

		void tftInit() {
			tft.begin();
			tftClear();
			pinMode(TFT_BL, OUTPUT);
			digitalWrite(TFT_BL, HIGH);
			useTFT = true;
		}

#endif // end of board-specific sections

static int hasTFT() {
	#if defined(OLED_128_64)
		return hasOLED;
	#endif
	// char s[100];
	// sprintf(s,"in hasTFT(): useTFT %d",useTFT);
	// outputString(s);
	return useTFT;
}

//sodb
// set this buffer to fixed value
// take TFT_WIDTH * 4 --> 4*320=1280
// #define BUFFER_PIXELS_SIZE (TFT_WIDTH * 8)
#define BUFFER_PIXELS_SIZE (8*320)

uint16_t bufferPixels[BUFFER_PIXELS_SIZE]; // used by primPixelRow and primDrawBuffer

static int color24to16b(int color24b) {
	// Convert 24-bit RGB888 format to the TFT's target pixel format.
	// Return [0..1] for 1-bit display, [0-255] for grayscale, and RGB565 for 16-bit color.

	int r, g, b;

	#ifdef IS_MONOCHROME
		return color24b ? 1 : 0;
	#endif

	#ifdef IS_GRAYSCALE
		r = (color24b >> 16) & 0xFF;
		g = (color24b >> 8) & 0xFF;
		b = color24b & 0xFF;
		int gray = r;
		if (g > r) gray = g;
		if (b > r) gray = b;
		return gray;
	#endif

	r = (color24b >> 19) & 0x1F; // 5 bits
	g = (color24b >> 10) & 0x3F; // 6 bits
	b = (color24b >> 3) & 0x1F; // 5 bits
	#if defined(ARDUINO_M5Stick_C) && !defined(ARDUINO_M5Stick_Plus)
		return (b << 11) | (g << 5) | r; // color order: BGR
	#else
		return (r << 11) | (g << 5) | b; // color order: RGB
	#endif
}

void tftClear() {
	// char s[100];
	// sprintf(s,"hasTFT %d",hasTFT());
	// outputString(s);
	if (!hasTFT()) return;

	tft.fillScreen(BLACK);
	UPDATE_DISPLAY();
}

void tftSetHugePixel(int x, int y, int state) {
	if (!useTFT) return;

	// simulate a 5x5 array of square pixels like the micro:bit LED array
	#if defined(PICO_ED)
		if ((1 <= x) && (x <= 5) && (1 <= y) && (y <= 5)) {
			int brightness = (state ? 100 : 0);
			tft.drawPixel((x + 5), y, brightness);
			UPDATE_DISPLAY();
		}
		return;
	#endif
	int minDimension, xInset = 0, yInset = 0;
	if (tft.width() > tft.height()) {
		minDimension = tft.height();
		xInset = (tft.width() - tft.height()) / 2;
	} else {
		minDimension = tft.width();
		yInset = (tft.height() - tft.width()) / 2;
	}
	int lineWidth = (minDimension > 60) ? 3 : 1;
	int squareSize = (minDimension - (6 * lineWidth)) / 5;
	tft.fillRect(
		xInset + ((x - 1) * squareSize) + (x * lineWidth), // x
		yInset + ((y - 1) * squareSize) + (y * lineWidth), // y
		squareSize, squareSize,
		color24to16b(state ? mbDisplayColor : BLACK));
	UPDATE_DISPLAY();
}

void tftSetHugePixelBits(int bits) {
	if (!useTFT) return;

	#if defined(PICO_ED)
		tft.clearDisplayBuffer();
		tft.showMicroBitPixels(bits, 1, 1);
		return;
	#endif
	if (0 == bits) {
		tftClear();
	} else {
		deferUpdates = true;
		for (int x = 1; x <= 5; x++) {
			for (int y = 1; y <= 5; y++) {
				tftSetHugePixel(x, y, bits & (1 << ((5 * (y - 1) + x) - 1)));
			}
		}
		deferUpdates = false;
	}
	UPDATE_DISPLAY();
}

OBJ primSetBacklight(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	if ((argCount < 1) || !isInt(args[0])) return falseObj;
	int brightness = obj2int(args[0]);
	(void) (brightness); // reference var to suppress compiler warning

	#if defined(ARDUINO_IOT_BUS)
		pinMode(33, OUTPUT);
		digitalWrite(33, (brightness > 0) ? HIGH : LOW);
	#elif defined(COCUBE)
		pinMode(TFT_BL, OUTPUT);
		if (brightness < 0) brightness = 0;
		if (brightness > 10) brightness = 10;
		analogWrite(TFT_BL, brightness * 25);
	#elif defined(ARDUINO_M5Stack_Core_ESP32)
		pinMode(32, OUTPUT);
		digitalWrite(32, (brightness > 0) ? HIGH : LOW);
	#elif defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_Plus)
		brightness = (brightness <= 0) ? 0 : brightness + 7; // 8 is lowest setting that turns on backlight
		if (brightness > 15) brightness = 15;
		int n = readAXP(0x28);
		writeAXP(0x28, (brightness << 4) | (n & 0x0f)); // set brightness (high 4 bits of reg 0x28)
	#elif defined(ARDUINO_NRF52840_CLUE)
		pinMode(34, OUTPUT);
		digitalWrite(34, (brightness > 0) ? HIGH : LOW);
	#elif defined(TTGO_RP2040)
		pinMode(TFT_BL, OUTPUT);
		if (brightness < 0) brightness = 0;
		if (brightness > 10) brightness = 10;
		analogWrite(TFT_BL, brightness * 25);
	#elif defined(OLED_128_64)
		int oledLevel = (255 * brightness) / 10;
		if (oledLevel < 0) oledLevel = 0;
		if (oledLevel > 255) oledLevel = 255;
		writeI2CReg(TFT_ADDR, 0x80, 0x81);
		writeI2CReg(TFT_ADDR, 0x80, oledLevel);
	#endif
	return falseObj;
}

static OBJ primGetWidth(int argCount, OBJ *args) {
	if (!hasTFT()) return zeroObj;

	#ifdef TFT_WIDTH
		return int2obj(TFT_WIDTH);
	#else
		return int2obj(0);
	#endif
}

static OBJ primGetHeight(int argCount, OBJ *args) {
	if (!hasTFT()) return zeroObj;

	#ifdef TFT_HEIGHT
		return int2obj(TFT_HEIGHT);
	#else
		return int2obj(0);
	#endif
}

static OBJ primSetPixel(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int x = obj2int(args[0]);
	int y = obj2int(args[1]);
	int color16b = color24to16b(obj2int(args[2]));
	tft.drawPixel(x, y, color16b);
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primPixelRow(int argCount, OBJ *args) {
	// Draw a single row of pixels (a list or byte array) at the given y.
	// If a byte array is provided the optional argument bytesPerPixel
	// determines the pixel size: 2, 3 or 4 bytes.
	// 2 means 16-bit RGB565 pixels; -2 means 16-bit RGB555 pixels.
	// 32 and 24 bit pixels are RGB(A) byte order. (Alpha of 32-bit pixels is ignored).
	// Used to accelerate BMP file display and other bitmap operations.

	if (!hasTFT()) return falseObj;

	OBJ pixelDataObj = args[0];
	int x = obj2int(args[1]);
	if (x >= TFT_WIDTH) return falseObj;
	int y = obj2int(args[2]);
	if ((y < 0) || (y >= TFT_HEIGHT)) return falseObj;
	int bytesPerPixel = ((argCount > 3) && isInt(args[3])) ? obj2int(args[3]) : 4;

	uint32 palette[256];
	if ((argCount > 4) && IS_TYPE(args[4], ListType)) {
		// paletteObj is a list of Integers representingRGB colors
		// palette is a C array of TFT display pixel values (e.g. 16-bit colors)
		OBJ paletteObj = args[4];
		int colorCount = obj2int(FIELD(paletteObj, 0)); // list size
		if (colorCount > 256) colorCount = 256;
		memset(palette, 0, sizeof(palette));
		for (int i = 0; i < colorCount; i++) {
			int rgb = obj2int(FIELD(paletteObj, i + 1));
			palette[i] = color24to16b(rgb & 0xFFFFFF);
		}
	}

	if (IS_TYPE(pixelDataObj, ListType)) {
		int pixelCount = obj2int(FIELD(pixelDataObj, 0));
		if (pixelCount > (TFT_WIDTH - x)) pixelCount = TFT_WIDTH - x;
		if (pixelCount > BUFFER_PIXELS_SIZE) pixelCount = BUFFER_PIXELS_SIZE;
		for (int i = 0; i < pixelCount; i++) {
			OBJ pixelObj = FIELD(pixelDataObj, (i + 1));
			bufferPixels[i] = (isInt(pixelObj)) ? color24to16b(obj2int(pixelObj)) : 0;
		}
		#if defined(TFT_ESPI)
		   //tft.pushImageDMA(x,y,pixelCount,1,bufferPixels);
		   // no DMA possible from PSRAM
		   tft.pushImage(x,y,pixelCount,1,bufferPixels);
		#elif defined(TFT_CONFIG) 
		tft.draw16bitRGBBitmap(x, y, bufferPixels, pixelCount, 1);
		#else
		tft.drawRGBBitmap(x, y, bufferPixels, pixelCount, 1);
		#endif
	} else if (IS_TYPE(pixelDataObj, ByteArrayType)) {
		int isRGB565 = true;
		if (bytesPerPixel < 0) {
			isRGB565 = false; // -2 means 16-bit RGB555 (vs. RGB565)
			bytesPerPixel = -bytesPerPixel;
		}
		if ((bytesPerPixel < 1) || (bytesPerPixel > 4)) return falseObj;

		int pixelCount = BYTES(pixelDataObj) / bytesPerPixel;
		if (pixelCount > (TFT_WIDTH - x)) pixelCount = TFT_WIDTH - x;
		if (pixelCount > BUFFER_PIXELS_SIZE) pixelCount = BUFFER_PIXELS_SIZE;
		uint8 *byte = (uint8 *) &FIELD(pixelDataObj, 0);
		if (1 == bytesPerPixel) {
			for (int i = 0; i < pixelCount; i++) {
				bufferPixels[i] = palette[*byte++];
			}
		} else if (2 == bytesPerPixel) {
			for (int i = 0; i < pixelCount; i++) {
				int pixel = (byte[1] << 8) | byte[0];
				int r = isRGB565 ? ((pixel >> 8) & 248) : ((pixel >> 7) & 248);
				int g = isRGB565 ? ((pixel >> 3) & 252) : ((pixel >> 2) & 248);
				int b = (pixel << 3) & 248;
				bufferPixels[i] = color24to16b((r << 16) | (g << 8) | b);
				byte += bytesPerPixel;
			}
		} else { // 24-bit or 32-bit pixels
			for (int i = 0; i < pixelCount; i++) {
				bufferPixels[i] = color24to16b((byte[2] << 16) | (byte[1] << 8) | byte[0]);
				byte += bytesPerPixel;
			}
		}
		#if defined(TFT_ESPI)
		   //tft.pushImageDMA(x,y,pixelCount,1,bufferPixels);
		   // no DMA possible from PSRAM
		   tft.pushImage(x,y,pixelCount,1,bufferPixels);
		#elif defined(TFT_CONFIG) 
		tft.draw16bitRGBBitmap(x, y, bufferPixels, pixelCount, 1);
		#else
		tft.drawRGBBitmap(x, y, bufferPixels, pixelCount, 1);
		#endif
	}
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primLine(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int x0 = obj2int(args[0]);
	int y0 = obj2int(args[1]);
	int x1 = obj2int(args[2]);
	int y1 = obj2int(args[3]);
	int color16b = color24to16b(obj2int(args[4]));
	tft.drawLine(x0, y0, x1, y1, color16b);
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primRect(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int x = obj2int(args[0]);
	int y = obj2int(args[1]);
	int width = obj2int(args[2]);
	int height = obj2int(args[3]);
	int color16b = color24to16b(obj2int(args[4]));
	int fill = (argCount > 5) ? (trueObj == args[5]) : true;
	if (fill) {
		tft.fillRect(x, y, width, height, color16b);
	} else {
		tft.drawRect(x, y, width, height, color16b);
	}
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primRoundedRect(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int x = obj2int(args[0]);
	int y = obj2int(args[1]);
	int width = obj2int(args[2]);
	int height = obj2int(args[3]);
	int radius = obj2int(args[4]);
	int color16b = color24to16b(obj2int(args[5]));
	int fill = (argCount > 6) ? (trueObj == args[6]) : true;
	if (fill) {
		tft.fillRoundRect(x, y, width, height, radius, color16b);
	} else {
		tft.drawRoundRect(x, y, width, height, radius, color16b);
	}
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primCircle(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int x = obj2int(args[0]);
	int y = obj2int(args[1]);
	int radius = obj2int(args[2]);
	int color16b = color24to16b(obj2int(args[3]));
	int fill = (argCount > 4) ? (trueObj == args[4]) : true;
	if (fill) {
		tft.fillCircle(x, y, radius, color16b);
	} else {
		tft.drawCircle(x, y, radius, color16b);
	}
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primTriangle(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int x0 = obj2int(args[0]);
	int y0 = obj2int(args[1]);
	int x1 = obj2int(args[2]);
	int y1 = obj2int(args[3]);
	int x2 = obj2int(args[4]);
	int y2 = obj2int(args[5]);
	int color16b = color24to16b(obj2int(args[6]));
	int fill = (argCount > 7) ? (trueObj == args[7]) : true;
	if (fill) {
		tft.fillTriangle(x0, y0, x1, y1, x2, y2, color16b);
	} else {
		tft.drawTriangle(x0, y0, x1, y1, x2, y2, color16b);
	}
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primText(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	OBJ value = args[0];
	int x = obj2int(args[1]);
	int y = obj2int(args[2]);
	int color16b = color24to16b(obj2int(args[3]));
	int scale = (argCount > 4) ? obj2int(args[4]) : 2;
	int wrap = (argCount > 5) ? (trueObj == args[5]) : true;
	int bgColor = (argCount > 6) ? color24to16b(obj2int(args[6])) : -1;
	tft.setCursor(x, y);
	tft.setTextColor(color16b);
	tft.setTextSize(scale);
	tft.setTextWrap(wrap);

	int lineH = 8 * scale;
	int letterW = 6 * scale;
	if (IS_TYPE(value, StringType)) {
	char *str = obj2str(value);
	if (bgColor != -1) tft.fillRect(x, y, strlen(str) * letterW, lineH, bgColor);
		tft.print(obj2str(value));
	} else if (trueObj == value) {
		if (bgColor != -1) tft.fillRect(x, y, 4 * letterW, lineH, bgColor);
		tft.print("true");
	} else if (falseObj == value) {
		if (bgColor != -1) tft.fillRect(x, y, 5 * letterW, lineH, bgColor);
		tft.print("false");
	} else if (isInt(value)) {
		char s[50];
		sprintf(s, "%d", obj2int(value));
		if (bgColor != -1) tft.fillRect(x, y, strlen(s) * letterW, lineH, bgColor);
		tft.print(s);
	}
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primClear(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;
	tftClear();
	return falseObj;
}

// Aruco and April tags

const uint16_t aruco_tags[100] = {
	0X4ACD, 0XF065, 0XCCD2, 0X66B9, 0XAB61, 0X8632, 0X61D1, 0X3B0D, 0X0125, 0X30A9, 0X066E, 0XEE58, 0XF148,
	0XD5F0, 0XDB4E, 0XD9C1, 0XB99A, 0X99FF, 0X93A1, 0X8950, 0X7974, 0X4FD4, 0X332A, 0X227D, 0X01B8, 0X6B8E,
	0X531B, 0X5AAB, 0XDEDC, 0XCB90, 0XBBEA, 0XA84D, 0X6130, 0X0F34, 0XF751, 0XF6D6, 0XE78A, 0XFB00, 0XF209,
	0XE3A5, 0XE8E7, 0XD5D7, 0XCD73, 0XC74D, 0XDB17, 0XD114, 0XD2C0, 0XB49B, 0XAFD1, 0XAFEC, 0XAE6B, 0XAA97,
	0XA2BE, 0XA068, 0X97FE, 0X9798, 0XEDB,  0X9E16, 0X94ED, 0X901A, 0X9820, 0X81E4, 0X7F5F, 0X7CBB, 0X745D,
	0X6C85, 0X7B93, 0X7AD5, 0X7A63, 0X6376, 0X605E, 0X4483, 0X43FB, 0X49A4, 0X4037, 0X4854, 0X35E0, 0X369D,
	0X26A7, 0X2C2A, 0X3367, 0X385F, 0X3AC8, 0X16A2, 0X06DA, 0X0444, 0X11D5, 0X08B2, 0XCA8A, 0X7552, 0X89E8,
	0XF530, 0XF9B4, 0XD23E, 0XB627, 0XBC0B, 0XB0C9, 0XB02C, 0X961B, 0X8F38};

const uint64_t april_tags[100] = {
	0x0004064a19651ff1, 0x0004064a53f425b6, 0x0004064a8e832b7b, 0x0004064ac9123140, 0x0004064b03a13705,
	0x0004064b3e303cca, 0x0004064b78bf428f, 0x0004064bb34e4854, 0x0004064beddd4e19, 0x0004064c286c53de,
	0x0004064c62fb59a3, 0x0004064c9d8a5f68, 0x0004064d12a86af2, 0x0004064d4d3770b7, 0x0004064dc2557c41,
	0x0004064dfce48206, 0x0004064e377387cb, 0x0004064e72028d90, 0x0004064eac919355, 0x0004064f21af9edf,
	0x0004064fd15cb02e, 0x000406500bebb5f3, 0x00040650467abbb8, 0x00040650bb98c742, 0x00040650f627cd07,
	0x000406516b45d891, 0x00040651a5d4de56, 0x000406521af2e9e0, 0x000406525581efa5, 0x00040653052f00f4,
	0x000406533fbe06b9, 0x000406537a4d0c7e, 0x00040653ef6b1808, 0x0004065429fa1dcd, 0x0004065464892392,
	0x000406549f182957, 0x00040654d9a72f1c, 0x00040655143634e1, 0x000406554ec53aa6, 0x000406558954406b,
	0x00040655c3e34630, 0x00040655fe724bf5, 0x000406567390577f, 0x00040656ae1f5d44, 0x00040657233d68ce,
	0x00040657985b7458, 0x00040657d2ea7a1d, 0x00040658480885a7, 0x00040658bd269131, 0x00040659e1f1ae0a,
	0x0004065a919ebf59, 0x0004065bb669dc32, 0x0004065bf0f8e1f7, 0x0004065cdb34f90b, 0x0004065d15c3fed0,
	0x0004065d50530495, 0x0004065e3a8f1ba9, 0x0004065eea3c2cf8, 0x0004066049964f96, 0x000406608425555b,
	0x00040660beb45b20, 0x0004066133d266aa, 0x00040661e37f77f9, 0x000406621e0e7dbe, 0x00040662932c8948,
	0x00040662cdbb8f0d, 0x00040663084a94d2, 0x0004066342d99a97, 0x000406637d68a05c, 0x00040663f286abe6,
	0x0004066467a4b770, 0x00040664a233bd35, 0x00040664dcc2c2fa, 0x000406651751c8bf, 0x0004066551e0ce84,
	0x00040666b13af122, 0x00040666ebc9f6e7, 0x0004066760e80271, 0x00040668109513c0, 0x000406684b241985,
	0x00040668fad12ad4, 0x000406696fef365e, 0x00040669aa7e3c23, 0x00040669e50d41e8, 0x0004066bb9857010,
	0x0004066bf41475d5, 0x0004066c6932815f, 0x0004066ca3c18724, 0x0004066d536e9873, 0x0004066dc88ca3fd,
	0x0004066e031ba9c2, 0x0004066eb2c8bb11, 0x000406704cb1e374, 0x00040670c1cfeefe, 0x00040670fc5ef4c3,
	0x0004067136edfa88, 0x00040671ac0c0612, 0x00040673bb1339ff, 0x000406746ac04b4e, 0x00040676b4568500};

const int april_bit_x[52] = {
	0, 1, 2, 3, 4, 5, 6, 7, 8, 3, 4, 5, 4, 9, 9, 9, 9, 9, 9, 9, 9, 9, 6, 6, 6, 5,
	9, 8, 7, 6, 5, 4, 3, 2, 1, 6, 5, 4, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 4};
const int april_bit_y[52] = {
	0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 3, 3, 4, 0, 1, 2, 3, 4, 5, 6, 7, 8, 3, 4, 5, 4,
	9, 9, 9, 9, 9, 9, 9, 9, 9, 6, 6, 6, 5, 9, 8, 7, 6, 5, 4, 3, 2, 1, 6, 5, 4, 5};

static OBJ primAruco(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int aruco_id = evalInt(args[0]);
	if (aruco_id >= 100) {
		return falseObj;
	}
	tft.drawRect(0, 0, TFT_HEIGHT, TFT_HEIGHT, BLACK);
	const int cellSize = TFT_HEIGHT/8;
	const int startX = TFT_WIDTH/2 - (4 * cellSize);
	uint16_t tag = aruco_tags[aruco_id];
	for (int i = 0; i < 8; i++) {
		for (int j = 0; j < 8; j++) {
			bool isBlack = false;

			if (i == 0 || i == 7 || j == 0 || j == 7) {
				// 外层白色边框 (outer white border)
				isBlack = false;
			} else if (i == 1 || i == 6 || j == 1 || j == 6) {
				// 内层黑色边框 (inner black border)
				isBlack = true;
			} else {
				// 中央的4x4区域，用于编码信息 (central 4x4 area for encoding information)
				int bitIndex = (i - 2) * 4 + (j - 2);
				isBlack = tag & (1 << (15 - bitIndex));
			}
			if (isBlack) {
				tft.fillRect(startX + j * cellSize, i * cellSize, cellSize, cellSize, BLACK);
			} else {
				tft.fillRect(startX + j * cellSize, i * cellSize, cellSize, cellSize, WHITE);
			}
		}
	}
	tft.setCursor(startX + 2, 2);
	tft.setTextColor(BLACK);
	tft.setTextSize(2);
	tft.print(aruco_id);
	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primAprilTag(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	int tag_id = evalInt(args[0]);
	if (tag_id >= 100) {
		return falseObj;
	}
	tft.drawRect(0, 0, TFT_HEIGHT, TFT_HEIGHT, BLACK);
	const int cellSize = TFT_HEIGHT/10;
	const int startX = TFT_WIDTH/2 - (5 * cellSize);
	uint64_t codedata = april_tags[tag_id];

	// 绘制外圈的黑色方块 (draw outer black square)
	for (int i = 1; i < 9; i++) {
		tft.fillRect(startX + i * cellSize, 1 * cellSize, cellSize, cellSize, BLACK); // 左边界 (left)
		tft.fillRect(startX + i * cellSize, 8 * cellSize, cellSize, cellSize, BLACK); // 右边界 (right)
		tft.fillRect(startX + 1 * cellSize, i * cellSize, cellSize, cellSize, BLACK); // 上边界 (top)
		tft.fillRect(startX + 8 * cellSize, i * cellSize, cellSize, cellSize, BLACK); // 下边界 (bottom)
	}

	// 绘制内圈的白色方块 (draw inner ring of white squares)
	for (int i = 2; i < 8; i++) {
		tft.fillRect(startX + i * cellSize, 2 * cellSize, cellSize, cellSize, WHITE); // 左边界 (left)
		tft.fillRect(startX + i * cellSize, 7 * cellSize, cellSize, cellSize, WHITE); // 右边界 (right)
		tft.fillRect(startX + 2 * cellSize, i * cellSize, cellSize, cellSize, WHITE); // 上边界 (top)
		tft.fillRect(startX + 7 * cellSize, i * cellSize, cellSize, cellSize, WHITE); // 下边界 (bottom)
	}

	// 绘制编码的标签图像 (draw encoded label)
	for (int i = 0; i < 52; i++) {
		int x = april_bit_x[i];
		int y = april_bit_y[i];
		bool bit = (codedata >> (51 - i)) & 1;
		uint16_t color = bit ? WHITE : BLACK;
		tft.fillRect(startX + x * cellSize, y * cellSize, cellSize, cellSize, color);
	}
	UPDATE_DISPLAY();
	return falseObj;
}

// display update control

OBJ primDeferUpdates(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;
	deferUpdates = true;
	return falseObj;
}

OBJ primResumeUpdates(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;
	deferUpdates = false;
	UPDATE_DISPLAY();
	return falseObj;
}

// 8 bit bitmap ops

static OBJ primMergeBitmap(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	OBJ bitmap = args[0];
	int bitmapWidth = obj2int(args[1]);
	OBJ buffer = args[2];
	int scale = max(min(obj2int(args[3]), 8), 1);
	int alphaIndex = obj2int(args[4]);
	int destX = obj2int(args[5]);
	int destY = obj2int(args[6]);

	int bitmapHeight = BYTES(bitmap) / bitmapWidth;
	int bufferWidth = TFT_WIDTH / scale;
	int bufferHeight = TFT_HEIGHT / scale;
	uint8 *bitmapBytes = (uint8 *) &FIELD(bitmap, 0);
	uint8 *bufferBytes = (uint8 *) &FIELD(buffer, 0);

	for (int y = 0; y < bitmapHeight; y++) {
		if ((y + destY) < bufferHeight && (y + destY) >= 0) {
			for (int x = 0; x < bitmapWidth; x++) {
				if ((x + destX) < bufferWidth && (x + destX) >= 0) {
					int pixelValue = bitmapBytes[y * bitmapWidth + x];
					if (pixelValue != alphaIndex) {
						int bufIndex = (destY + y) * bufferWidth + x + destX;
						bufferBytes[bufIndex] = pixelValue;
					}
				}
			}
		}
	}
	return falseObj;
}

static OBJ primDrawBuffer(int argCount, OBJ *args) {
	if (!hasTFT()) return falseObj;

	OBJ buffer = args[0];
	OBJ palette = args[1]; // List, index-1 based
	int scale = max(min(obj2int(args[2]), 8), 1);

	int originX = 0;
	int originY = 0;
	int copyWidth = -1;
	int copyHeight = -1;

	if (argCount > 6) {
		originX = obj2int(args[3]);
		originY = obj2int(args[4]);
		copyWidth = obj2int(args[5]);
		copyHeight = obj2int(args[6]);
	}

	int bufferWidth = TFT_WIDTH / scale;
	int bufferHeight = TFT_HEIGHT / scale;

	int originWidth = copyWidth >= 0 ? copyWidth : bufferWidth;
	int originHeight = copyHeight >= 0 ? copyHeight : bufferHeight;

	uint8 *bufferBytes = (uint8 *) &FIELD(buffer, 0);
	// Read the indices from the buffer and turn them into color values from the
	// palette, and paint them onto the TFT
	for (int y = 0; y < originHeight; y ++) {
		for (int x = 0; x < originWidth; x ++) {
			int colorIndex = bufferBytes[
				(y + originY) * bufferWidth + (x + originX)];
			int color = color24to16b(obj2int(FIELD(palette, colorIndex + 1)));
			for (int i = 0; i < scale; i ++) {
				for (int j = 0; j < scale; j ++) {
					bufferPixels[(j * originWidth * scale) + x * scale + i] = color;
				}
			}
		}

		#if defined(TFT_ESPI)
		   //tft.pushImageDMA(x,y,pixelCount,1,bufferPixels);
		   // no DMA possible from PSRAM
			tft.pushImage(
				originX * scale,
				(originY + y) * scale,
				originWidth * scale,
				scale,
				bufferPixels
			);
		#elif defined(TFT_CONFIG) 
			tft.draw16bitRGBBitmap(
			originX * scale,
			(originY + y) * scale,
			bufferPixels,
			originWidth * scale,
			scale
			);
		#else
			tft.drawRGBBitmap(
				originX * scale,
				(originY + y) * scale,
				bufferPixels,
				originWidth * scale,
				scale
			);
		#endif
	}

	UPDATE_DISPLAY();
	return falseObj;
}

static OBJ primDrawBitmap(int argCount, OBJ *args) {
	// Draw an 8-bit bitmap at a given position without scaling.

	if (!hasTFT()) return falseObj;
	uint32 palette[256];

	if (argCount < 4) return fail(notEnoughArguments);
	OBJ bitmapObj = args[0]; // bitmap: a two-item list of [width (int), pixels (byte array)]
	OBJ paletteObj = args[1]; // palette: a list of RGB values
	int dstX = obj2int(args[2]);
	int dstY = obj2int(args[3]);

	if ((dstX > TFT_WIDTH) || (dstY > TFT_HEIGHT)) return falseObj; // off screen

	// process bitmap arg
	if (!IS_TYPE(bitmapObj, ListType) ||
	 	(obj2int(FIELD(bitmapObj, 0)) != 2) ||
	 	!isInt(FIELD(bitmapObj, 1)) ||
	 	!IS_TYPE(FIELD(bitmapObj, 2), ByteArrayType)) {
	 		return fail(bad8BitBitmap);
	}
	int bitmapWidth = obj2int(FIELD(bitmapObj, 1));
	OBJ bitmapBytesObj = FIELD(bitmapObj, 2);
	int bitmapByteCount = BYTES(bitmapBytesObj);
	if ((bitmapWidth <= 0) || ((bitmapByteCount % bitmapWidth) != 0)) return fail(bad8BitBitmap);
	int bitmapHeight = bitmapByteCount / bitmapWidth;

	// process palette arg
	if (!IS_TYPE(paletteObj, ListType)) return fail(badColorPalette);
	int colorCount = obj2int(FIELD(paletteObj, 0)); // list size
	if (colorCount > 256) colorCount = 256;
	memset(palette, 0, sizeof(palette)); // initialize to all black RGB values
	for (int i = 0; i < colorCount; i++) {
		int rgb = obj2int(FIELD(paletteObj, i + 1));
		if (rgb < 0) rgb = 0;
		if (rgb > 0xFFFFFF) rgb = 0xFFFFFF;
		palette[i] = rgb;
	}

	int srcX = 0;
	int srcW = bitmapWidth;
	if (dstX < 0) { srcX = -dstX; dstX = 0; srcW -= srcX; }
	if (srcW < 0) return falseObj; // off screen to left
	if ((dstX + srcW) > TFT_WIDTH) srcW = TFT_WIDTH - dstX;

	int srcY = 0;
	int srcH = bitmapHeight;
	if (dstY < 0) { srcY = -dstY; dstY = 0; srcH -= srcY; }
	if (srcH < 0) return falseObj; // off screen above
	if ((dstY + srcH) > TFT_HEIGHT) srcH = TFT_HEIGHT - dstY;

	uint8 *bitmapBytes = (uint8 *) &FIELD(bitmapBytesObj, 0);
	for (int i = 0; i < srcH; i++) {
		uint8 *row = bitmapBytes + ((srcY + i) * bitmapWidth);
		for (int j = 0; j < srcW; j++) {
			uint8 pix = row[srcX + j]; // 8-bit color index
			uint32 rgb = palette[pix]; // 24 bit RGB color
			tft.drawPixel(dstX + j, dstY + i, color24to16b(rgb));
		}
	}
	UPDATE_DISPLAY();
	return falseObj;
}


#else // stubs

void tftInit() { }
void tftClear() { }
void tftSetHugePixel(int x, int y, int state) { }
void tftSetHugePixelBits(int bits) { }

static OBJ primSetBacklight(int argCount, OBJ *args) { return falseObj; }
static OBJ primGetWidth(int argCount, OBJ *args) { return int2obj(0); }
static OBJ primGetHeight(int argCount, OBJ *args) { return int2obj(0); }
static OBJ primSetPixel(int argCount, OBJ *args) { return falseObj; }
static OBJ primPixelRow(int argCount, OBJ *args) { return falseObj; }
static OBJ primLine(int argCount, OBJ *args) { return falseObj; }
static OBJ primRect(int argCount, OBJ *args) { return falseObj; }
static OBJ primRoundedRect(int argCount, OBJ *args) { return falseObj; }
static OBJ primCircle(int argCount, OBJ *args) { return falseObj; }
static OBJ primTriangle(int argCount, OBJ *args) { return falseObj; }
static OBJ primText(int argCount, OBJ *args) { return falseObj; }
static OBJ primClear(int argCount, OBJ *args) { return falseObj; }

OBJ primDeferUpdates(int argCount, OBJ *args) { return falseObj; }
OBJ primResumeUpdates(int argCount, OBJ *args) { return falseObj; }

static OBJ primMergeBitmap(int argCount, OBJ *args) { return falseObj; }
static OBJ primDrawBuffer(int argCount, OBJ *args) { return falseObj; }
static OBJ primDrawBitmap(int argCount, OBJ *args) { return falseObj; }

static OBJ primAruco(int argCount, OBJ *args) { return falseObj; }
static OBJ primAprilTag(int argCount, OBJ *args) { return falseObj; }

#endif

//LVGL


#if defined(LVGL) 
#include <lvgl.h>
extern bool useLVGL;
extern bool LVGL_initialized;
void setup_lvgl(void); 

static uint32_t screenWidth;
static uint32_t screenHeight;

#if defined(TFT_ESPI)

	void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
		uint16_t w = area->x2 - area->x1 + 1;
		uint16_t h = area->y2 - area->y1 + 1;

		uint16_t *color_buf = (uint16_t *)px_map;

		tft.startWrite();
		tft.setAddrWindow(area->x1, area->y1, w, h);

		// Send entire area with DMA
		#if defined(S3_CTF)
		  tft.pushPixels(color_buf, w * h);      // ← NOT DMA
		  tft.endWrite();
		  
		#else

		   tft.pushPixelsDMA(color_buf, w * h);

		   tft.endWrite();
		   tft.dmaWait();
		#endif
		// Immediately notify LVGL since TFT_eSPI handles DMA behind the scenes
		lv_disp_flush_ready(disp);
		yield(); // sodb give wifi some air to breath
	}

#else

void my_disp_flush(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
//#ifndef DIRECT_RENDER_MODE
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);

  tft.draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)px_map, w, h);
  
//#endif // #ifndef DIRECT_RENDER_MODE

  /*Call it to tell LVGL you are ready*/
  lv_disp_flush_ready(disp);
}
#endif
void my_touchpad_read(lv_indev_t *indev, lv_indev_data_t *data)
{
			if (screenTouched()) {
				data->state = LV_INDEV_STATE_PRESSED;
				data->point.x = screenTouchX();
				data->point.y = screenTouchY();
			
			} else {
			data->state = LV_INDEV_STATE_RELEASED;
			}
		
}

#define TFT_BUFFER_LINES 40
static lv_draw_buf_t draw_buf;
static lv_color_t *buf1;
static lv_color_t *buf2;

bool event_seen = false;

static lv_display_t * disp;

// some includes for c++ maps etc.
#include <unordered_map>
#include <string>
#include <functional>
#include <vector>


template<typename T>
class ObjectRegistry {
public:
    void add(const std::string& name, T* obj) {
        registry[name] = obj;
    }

	

    T* get(const std::string& name) const {
		auto it = registry.find(name);
        return it != registry.end() ? it->second : nullptr;
	}

    bool remove(const std::string& name) {
        return registry.erase(name) > 0;
    }

    void printall() {
         for (const auto& pair : registry) {
             char s[100];
			sprintf(s,"name %s ",pair.first.c_str());
			outputString(s);
    	}
	}

    size_t size() const {
        return registry.size();
    }

	std::vector<std::string> getAllNames() const {
        std::vector<std::string> names;
        for (const auto& entry : registry) {
            names.push_back(entry.first);
        }
        return names;
    }

	std::string findNameFor(T* obj) const {
        for (const auto& pair : registry) {
            if (pair.second == obj) {
                return pair.first;
				//  char s[100];
				// sprintf(s,"find name %s ",pair.first.c_str());
				// outputString(s);
            }
        }
        return "";
    }

private:
    std::unordered_map<std::string, T*> registry;
};

template <>
lv_obj_t* ObjectRegistry<lv_obj_t>::get(const std::string& name) const {
    if (name == "lv_scr_act") {
        return lv_scr_act();
    } else {
        auto it = registry.find(name);
        return it != registry.end() ? it->second : nullptr;
    }
}


ObjectRegistry<lv_obj_t>  registry;

ObjectRegistry<lv_font_t> font_buffer;

ObjectRegistry<lv_style_t> style_registry;

ObjectRegistry<lv_chart_series_t> series_registry;

ObjectRegistry<char*> btnmap_registry;

 
void fs_init() {

    if (!LittleFS.begin()) {
        outputString("⚠️ LittleFS mount failed, formatting...");
        if (!LittleFS.format()) {
            outputString("❌ LittleFS format failed!");
        }
        if (!LittleFS.begin()) {
            outputString("❌ LittleFS mount failed again after format!");
        }
    }
    outputString("✅ LittleFS mounted successfully.");

}

void setup_lvgl() {

	if (useTFT) {
  	#include "esp_heap_caps.h"
	lv_init();
	// double buffer
	
 	size_t buf_size = TFT_WIDTH * TFT_BUFFER_LINES * sizeof(lv_color_t);
 	// char s[100];
	//  sprintf(s,"free heap before: %d psram: %d ",  ESP.getFreeHeap(),ESP.getFreePsram());
	//  outputString(s);
	#if defined (CYDIO)
		#if defined(BLE_IDE)
    		#define LV_NR_ROWS 10
		#else
			#define LV_NR_ROWS 40
		#endif		
		  // reduce number of rows from 40 to 10 in order to allow for Wifi+LVGL+BLE
	#elif defined(COCUBE)
		#define LV_NR_ROWS 10
	#else
		#if defined(TFT_ESPI)
			#define LV_NR_ROWS 40
		#else
			#if defined(BLE_IDE)
				#define LV_NR_ROWS 20
			#else
				#define LV_NR_ROWS 40
			#endif	
		#endif
	#endif
	// size_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	int psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM); // in bytes
	if (psramFree>0) {
		//Serial.printf("psramfree = %d\r\n",psramFree);
		buf1 = (lv_color_t *)heap_caps_malloc(TFT_WIDTH * LV_NR_ROWS * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
		buf2 = (lv_color_t *)heap_caps_malloc(TFT_WIDTH * LV_NR_ROWS * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

	} else {
		buf1 = (lv_color_t *)heap_caps_malloc(TFT_WIDTH * LV_NR_ROWS * sizeof(lv_color_t), MALLOC_CAP_DMA);
		buf2 = (lv_color_t *)heap_caps_malloc(TFT_WIDTH * LV_NR_ROWS * sizeof(lv_color_t), MALLOC_CAP_DMA);

	}
	
/* try to allocated psram first
void* ptr = heap_caps_malloc(
    1024,
    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
);

if (!ptr) {
    ptr = heap_caps_malloc(
        1024,
        MALLOC_CAP_8BIT
    );
}



*/

 	// if (buf1)  outputString("malloc succesfull");
	//  else outputString("cannot mallocsuccesfull");
	
	//   sprintf(s,"free heap after: %d psram: %d ",  ESP.getFreeHeap(),ESP.getFreePsram());
	//   outputString(s);

	disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
    lv_display_set_buffers(disp, buf1, buf2, TFT_WIDTH * LV_NR_ROWS, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, my_disp_flush);
	#if defined(TFT_ESPI) 
		#if defined(CYDROT)
			lv_display_set_resolution(disp, TFT_WIDTH, TFT_HEIGHT);
		#else
			lv_display_set_resolution(disp, TFT_HEIGHT, TFT_WIDTH);
		#endif
	#else
    	lv_display_set_resolution(disp, TFT_WIDTH, TFT_HEIGHT);
	#endif
   #if defined(HAS_TOUCH_SCREEN)
	/*Initialize the (dummy) input device driver*/
		lv_indev_t * indev = lv_indev_create();
		lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER); /*Touchpad should have POINTER type*/
		lv_indev_set_read_cb(indev, my_touchpad_read);
		if (!touchEnabled) touchInit();
	#endif
   #if defined(COCUBE)
    lv_indev_t * indev = lv_indev_create();
	lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD); /*Touchpad should have POINTER type*/
	lv_indev_set_read_cb(indev,keypad_read);
	
	lv_indev_set_long_press_time(indev, 400);        // ms until LV_EVENT_LONG_PRESSED
	lv_indev_set_long_press_repeat_time(indev, 100); // repeat interval in ms

    // Optional: create a group so widgets can get focus
	group = lv_group_create();
	// Attach the keypad input device to the group
	lv_indev_set_group(indev, group);
   #endif
 fs_init() ;
//lv_fs_littlefs_init();
	LVGL_initialized = true;
	// store main screen object in object with name '!main_screen_default'
	// hide this object from user in get_all_objects
	registry.add("!main_screen_default",lv_scr_act() );
	}
	useLVGL=false;
}


void set_lvgl(bool use_lvgl) {
	if (use_lvgl) {
		useLVGL=true;
		// refresh all objects
		lv_obj_invalidate(lv_scr_act());
	} else {
		useLVGL=false;
		tftClear();
	}
}

// dummy test for generating ticks
void lvgl_tick() {
 	lv_tick_inc(1);
     lv_timer_handler();
}


// in lvgl 9 there is no LV_EVENT_NONE defined, so define it ourselves
#define LV_EVENT_NONE_CUSTOM (lv_event_code_t)(-1)
struct LastEventInfo {
    lv_event_t *event;
    lv_event_code_t code;
    lv_obj_t *target;
	std::string  name;
	uint32_t id;
};


static LastEventInfo last_event = {
    nullptr,
    LV_EVENT_NONE_CUSTOM,
    nullptr,
    std::string(),   // default empty string
    0                // id
};

// generic call back function for all events
void ui_log_event_cb(lv_event_t *e) {
    last_event.event = e;
    last_event.code = lv_event_get_code(e);
    last_event.target = (lv_obj_t *) lv_event_get_target(e);
	if (lv_obj_get_class(last_event.target) == &lv_buttonmatrix_class) {
		last_event.id = lv_buttonmatrix_get_selected_button(last_event.target);
	}
	last_event.name = registry.findNameFor( last_event.target);
		// char s[100];
		// sprintf(s,"Event %d on obj name %s id %d", last_event.code, last_event.name.c_str(),last_event.id);
		// outputString(s);
	// // send broadcast
	event_seen = true; // set to false in getevent
	
	char eventmessage[] = "LVGLevent";
	// send a broadcast with text: LVGLevent
	startReceiversOfBroadcast(eventmessage, 9);
	sendBroadcastToIDE(eventmessage, 9);
}

int ui_get_last_event(std::string& name_out) {
    name_out = last_event.name;
    return static_cast<int>(last_event.code);
}


const lv_font_t* get_font_from_scale(int scale_x) {
    switch(scale_x) {
        case 1: return &lv_font_montserrat_14;
        case 2: return &lv_font_montserrat_24;
        case 3: return &lv_font_montserrat_40;
		case 4: return &lv_font_montserrat_48;
        default: return &lv_font_montserrat_14; // default fallback
    }
}




void ui_add_image(char * obj_name, const char *path, const char * parent_name) {
	lv_obj_t* parent = registry.get(parent_name);
	lv_obj_t* obj;
    // Create an img object
    
	if (!registry.get(obj_name) && parent) {
		lv_obj_t *obj = lv_img_create(lv_screen_active());
		// Set image source from file
		lv_img_set_src(obj, path);
		// outputString("ui_add_image");
		// outputString(path);
		// Optional: align or move the image
		//lv_obj_center(img);
		registry.add(obj_name, obj);
	}
}


void ui_add_font(char * obj_name, const char *path) {
	lv_obj_t* obj;
    // Create an img object
    
	if (!font_buffer.get(obj_name) ) {
		lv_font_t *obj = lv_binfont_create(path);
		// Set image source from file
		if (obj) {
			font_buffer.add(obj_name, obj);
		}
	}
}

/*
 void ui_add_image(char * obj_name, const char *path, const char * parent_name) {
	lv_obj_t* parent = registry.get(parent_name);
	lv_obj_t* obj;
    // Create an img object
    
	if (!registry.get(obj_name) && parent) {
		size_t size;
		uint8_t* buffer = load_file_to_psram(path,&size);
		lv_image_dsc_t *img_dsc = (lv_image_dsc_t *)buffer;
		lv_obj_t *obj = lv_img_create(lv_screen_active());
		// Set image source from file
		lv_img_set_src(obj, img_dsc);
		outputString("ui_add_image");
		outputString(path);
		// Optional: align or move the image
		//lv_obj_center(img);
		registry.add(obj_name, obj);
		img_buffer.add(obj_name, buffer);

	}
}

*/


void ui_create_button_label(char * obj_name, int scale, const char * label_text, const char * parent_name) {
	lv_obj_t* parent = registry.get(parent_name);
	lv_obj_t* obj;
	if (!registry.get(obj_name) && parent) {
		if (lv_obj_get_class(parent) == &lv_list_class) {
			obj = lv_list_add_button(parent, NULL, label_text);
		} else {
			obj = lv_btn_create(parent);
			// sodb: check whether label is correctly removed when parent btn object is deleted
			lv_obj_t * label = lv_label_create(obj);
			lv_label_set_text(label, label_text);
			lv_obj_set_style_text_font(label, get_font_from_scale(scale), LV_PART_MAIN);
			lv_obj_center(label);
		}
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_CLICKED, NULL);
		registry.add(obj_name, obj);
	}

}


void ui_create_button(char * obj_name, const char * parent) {
	if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_btn_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_CLICKED, NULL);
		// sodb: check whether label is correctly removes when partent btn object is deleted
		registry.add(obj_name, obj);
	}

}

void ui_create_label(char * obj_name, int scale, const char * label_text, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* label = lv_label_create(registry.get(parent));
		lv_label_set_text(label, label_text);
		lv_obj_set_style_text_font(label, get_font_from_scale(scale), LV_PART_MAIN);
		registry.add(obj_name, label);
	}
}


void ui_create_slider(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_slider_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_LONG_PRESSED, NULL);
		lv_slider_set_range(obj, 0, 100);
		registry.add(obj_name, obj);
	}
}

void ui_create_arc(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_arc_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_LONG_PRESSED, NULL);
		// sodb solve unmovable arc on capacitive touch displays.
		lv_obj_add_flag(obj, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_CHECKABLE ));
		registry.add(obj_name, obj);
	}
}


void ui_create_switch(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_switch_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}


void ui_create_led(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_led_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_bar(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_bar_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_tabview(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tabview_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_tileview(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tileview_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_screen(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		#if defined(LMSDISPLAY)
			#define _TFT_WIDTH TFT_HEIGHT
			#define _TFT_HEIGHT TFT_WIDTH
		#else
			#define _TFT_WIDTH TFT_WIDTH
			#define _TFT_HEIGHT TFT_HEIGHT
		#endif
		lv_obj_t* obj = lv_obj_create(0); // crete empty screen
		lv_obj_set_pos(obj, 0, 0);
		lv_obj_set_size(obj, _TFT_WIDTH, _TFT_HEIGHT);
		registry.add(obj_name, obj);
	}
}


void ui_create_roller(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_roller_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		lv_obj_add_event_cb(obj, ui_log_event_cb,LV_EVENT_LONG_PRESSED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_spinbox(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_spinbox_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_create_spinner(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_spinner_create(registry.get(parent));
		registry.add(obj_name, obj);
	}
}

void ui_create_scale(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_scale_create(registry.get(parent));
		registry.add(obj_name, obj);
	}
}

void ui_create_keyboard(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_keyboard_create(registry.get(parent));
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_READY, NULL);
		registry.add(obj_name, obj);
	}
}
void ui_create_textarea(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_textarea_create(registry.get(parent));
		registry.add(obj_name, obj);
	}
}

void ui_add_tab(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tabview_add_tab(registry.get(parent),obj_name);
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}

void ui_add_series(char * series, const char * chart, int color) {
    if (registry.get(chart) && !series_registry.get(series)) {
		lv_chart_series_t* obj = lv_chart_add_series(registry.get(chart), lv_color_hex(color),  LV_CHART_AXIS_PRIMARY_Y);
		//lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		series_registry.add(series, obj);
	}
}

void ui_set_next_value(char * series, char * chart, int val) {
    if (registry.get(chart) && series_registry.get(series)) {
		lv_chart_set_next_value(registry.get(chart), series_registry.get(series), val);
	}
}

void ui_set_next_value2(char * series, char * chart, int val, int val2) {
    if (registry.get(chart) && series_registry.get(series)) {
		lv_chart_set_next_value2(registry.get(chart), series_registry.get(series), val, val2);
		//outputString("lv_chart_set_next_value2");
	}
}


void ui_add_chart(char * obj_name, const char * parent, char * chart_type, char * chart_update_mode) {
	if (!registry.get(obj_name))  {
		lv_chart_type_t chart_type_id = LV_CHART_TYPE_LINE;
		lv_chart_update_mode_t chart_update_mode_id = LV_CHART_UPDATE_MODE_SHIFT;
		if (strcmp(chart_type,"bar")==0) chart_type_id = LV_CHART_TYPE_BAR;
		else if (strcmp(chart_type,"scatter")==0) chart_type_id = LV_CHART_TYPE_SCATTER;

		if (strcmp(chart_update_mode,"circular")==0) chart_update_mode_id = LV_CHART_UPDATE_MODE_CIRCULAR;
		lv_obj_t* obj = lv_chart_create(registry.get(parent));
		lv_chart_set_type(obj, chart_type_id);
		lv_chart_set_update_mode(obj, chart_update_mode_id);

		registry.add(obj_name, obj);
	}
}

void ui_add_tile(char * obj_name, const char * parent, int col_id, int row_id, lv_dir_t dir ) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_tileview_add_tile(registry.get(parent), col_id, row_id, dir);
		lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
}


void ui_create_list(char * obj_name, const char * parent) {
    if (!registry.get(obj_name) && registry.get(parent)) {
		lv_obj_t* obj = lv_list_create(registry.get(parent));
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		registry.add(obj_name, obj);
	}
	if (strcmp(parent,"lv_scr_act") !=0) {
		
	}
}

void ui_create_style(char * obj_name, const char * parent) {
    if (!style_registry.get(obj_name)) { 
		lv_style_t* obj = new lv_style_t;
		lv_style_init(obj);
		// lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
		style_registry.add(obj_name, obj);
	}
}

void ui_set_parent(char * obj_name, const char * parent, int states, int parts){
	lv_obj_t* obj =  registry.get(obj_name);
	lv_obj_t* obj_parent =  registry.get(parent);
	if (obj && obj_parent) {
		if ( (lv_obj_get_class(obj) == &lv_keyboard_class) &&
			 (lv_obj_get_class(obj_parent) == &lv_textarea_class) ) {
			 	lv_keyboard_set_textarea(obj, obj_parent);
			} else
				lv_obj_set_parent(registry.get(obj_name), obj_parent);
	} else
	if (style_registry.get(obj_name) && obj_parent) {
		lv_style_t* style =  style_registry.get(obj_name);
		lv_obj_add_style(obj_parent, style, states + parts);
		//outputString("style added");
		
	}
}


void free_btnmap(char **btnmap) {
    if (!btnmap) return;
    for (size_t i = 0; btnmap[i] != NULL; i++) {
        free(btnmap[i]);
    }
    free(btnmap);
}

void ui_delete_obj(char * obj_name) {
    lv_obj_t* obj = registry.get(obj_name);
	lv_font_t* font = font_buffer.get(obj_name);
	lv_style_t* style = style_registry.get(obj_name);
	lv_chart_series_t* chart_series = series_registry.get(obj_name);
	char** btnmap = btnmap_registry.get(obj_name);
	// lv_chart_series_t* series = series_registry.get(obj_name); 
	// not needed because lv_obj_del of chart already deletes all the series attached to the chart
    if (obj) {
		lv_obj_del(obj);
        registry.remove(obj_name);
    } 
	if (font){
			lv_binfont_destroy(font);
			font_buffer.remove(obj_name);
	} 
	if (style) { 
		delete style;
		style_registry.remove(obj_name);
	} 
	if (chart_series) {
		// lv_obj_del(obj); // slready deleted iwith parent chart
        series_registry.remove(obj_name);
	}
	if (btnmap) { 
		free_btnmap(btnmap); // free structure of char** for btnmap
		btnmap_registry.remove(obj_name); // remove entry in btnmap_registry
	}
}

void ui_set_size(char * obj_name,  lv_coord_t w, lv_coord_t h ) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		lv_obj_set_size(obj,w,h);
	}
}

void ui_set_pos(char * obj_name,  uint16_t pos_x, uint16_t pos_y) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj!=nullptr) {
		lv_obj_set_pos(obj,pos_x,pos_y);
	}
}

void ui_set_scroll(char * obj_name,  lv_dir_t scroll_dir) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj!=nullptr) {
		if (lv_obj_get_class(obj) == &lv_tabview_class) {
			// when tabview, take container of tabs to controll scroll direction
			lv_obj_t *content = lv_tabview_get_content(obj);
			lv_obj_set_scroll_dir(content, scroll_dir);
		} else
			  lv_obj_set_scroll_dir(obj, scroll_dir);
		}
}


void ui_set_value(char * obj_name, int value) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			lv_arc_set_value(obj, value);
		} else 
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			lv_slider_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_bar_class) {
			lv_bar_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			lv_spinbox_set_value(obj, value);
		} else
		if (lv_obj_get_class(obj) == &lv_switch_class) {
			if (value==0) lv_obj_remove_state(obj, LV_STATE_CHECKED);
			else if (value&1) lv_obj_add_state(obj, LV_STATE_CHECKED);
			else if (value>1) lv_obj_add_state(obj, (lv_state_t)value);
			else if (value<0) lv_obj_remove_state(obj,(lv_state_t) -value);
			
		} else
		if (lv_obj_get_class(obj) == &lv_led_class) {
			if (value==0) lv_led_off(obj);
			else if (value&1) lv_led_on(obj);
			
			
		} else
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_visible_row_count(obj,value);
		}
	}
}

void ui_set_text(char * obj_name, char * text, int scale) {
    lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_label_class) {
			lv_label_set_text(obj, text);
			lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_MAIN);
		} else 
		if (lv_obj_get_class(obj) == &lv_button_class) {
			lv_obj_t *label = lv_obj_get_child(obj, 0);
			if (label) {
				lv_label_set_text(label, text);
				lv_obj_set_style_text_font(label, get_font_from_scale(scale), LV_PART_MAIN);
			}
		} else 
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_options(obj, text, LV_ROLLER_MODE_INFINITE);
			lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_MAIN | LV_STATE_DEFAULT | LV_STYLE_PROP_FLAG_INHERITABLE);
			lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_SELECTED|  LV_STATE_DEFAULT);
			
		}else 
		if (lv_obj_get_class(obj) == &lv_textarea_class) {
			lv_textarea_set_text(obj, text);
			//lv_obj_set_style_text_font(obj, get_font_from_scale(scale), LV_PART_MAIN); // does not seem to work
		}
	}
}

void ui_set_text_font(char * obj_name, char * text, char * font_name) {
    lv_obj_t* obj = registry.get(obj_name);

				
	if (obj) {
		lv_font_t * font = font_buffer.get(font_name);
		if (lv_obj_get_class(obj) == &lv_label_class) {
			lv_label_set_text(obj, text);
			if (font) {
				lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
			}
		} else 
		if (lv_obj_get_class(obj) == &lv_button_class) {
			lv_obj_t *label = lv_obj_get_child(obj, 0);
			if (label) {
				lv_label_set_text(label, text);
				if (font)
					lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
			}
		} else 
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_options(obj, text, LV_ROLLER_MODE_INFINITE);
		}
	}
}


void ui_set_attribute(char * obj_name, char * attribute_name, int to_val, int until_val){
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (strcmp(attribute_name,"flags")==0) lv_obj_add_flag(obj, (lv_obj_flag_t)to_val); else
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			if (strcmp(attribute_name,"range")==0) lv_arc_set_range(obj, to_val, until_val);
			else if (strstr(attribute_name,"angles")) lv_arc_set_bg_angles(obj, to_val, until_val);
			else if (strstr(attribute_name,"rotation")) lv_arc_set_rotation(obj, to_val);
			else if (strstr(attribute_name,"line width")) {
				//outputString("line width");
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_MAIN);
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_INDICATOR);
			}
		} else 
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			if (strcmp(attribute_name,"range")==0) lv_slider_set_range(obj, to_val, until_val);
		} else
		if (lv_obj_get_class(obj) == &lv_bar_class) {
			if (strcmp(attribute_name,"range")==0) lv_bar_set_range(obj, to_val, until_val);
		} else
		if (lv_obj_get_class(obj) == &lv_spinner_class) {
			if (strcmp(attribute_name,"animation")==0) lv_spinner_set_anim_params(obj, to_val, until_val);
			else if (strstr(attribute_name,"line width")) {
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_MAIN);
				lv_obj_set_style_arc_width(obj,to_val,LV_PART_INDICATOR);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			if (strcmp(attribute_name,"range")==0) lv_spinbox_set_range(obj, to_val, until_val);
			if (strcmp(attribute_name,"digits")==0) lv_spinbox_set_digit_format(obj, to_val, until_val);
			if (strcmp(attribute_name,"increment")==0) lv_spinbox_increment(obj);
			if (strcmp(attribute_name,"decrement")==0) lv_spinbox_decrement(obj);
		} else
		if (lv_obj_get_class(obj) == &lv_led_class) {
			if (strcmp(attribute_name,"brightness")==0) {
				lv_led_set_brightness(obj,to_val );
			}
		} else 
		if (lv_obj_get_class(obj) == &lv_chart_class) {
			if (strcmp(attribute_name,"points")==0) {
				lv_chart_set_point_count(obj,to_val );
				//outputString("lv_chart_set_point_count");
			}
			if (strcmp(attribute_name,"range")==0) {
				lv_chart_set_range(obj, LV_CHART_AXIS_PRIMARY_Y, to_val, until_val);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_scale_class) {
			if (strcmp(attribute_name,"tick count")==0) lv_scale_set_total_tick_count(obj,to_val );
			if (strcmp(attribute_name,"major tick every")==0) lv_scale_set_major_tick_every(obj,to_val );
			if (strcmp(attribute_name,"length")==0) lv_obj_set_style_length(obj,to_val, until_val );
			if (strcmp(attribute_name,"range")==0) lv_scale_set_range(obj, to_val, until_val);
			if (strstr(attribute_name,"scale mode")) lv_scale_set_mode(obj, (lv_scale_mode_t) to_val);
			if (strstr(attribute_name,"angles")) lv_scale_set_angle_range(obj, to_val);
			if (strstr(attribute_name,"rotation")) lv_scale_set_rotation(obj, to_val);
			if (strcmp(attribute_name,"show labels")==0) {
				bool show_labels = (to_val==1);
				lv_scale_set_label_show(obj,show_labels);
			}
		} else
		if (lv_obj_get_class(obj) == &lv_buttonmatrix_class) {
			if (strcmp(attribute_name,"button ctrl")==0) {
				// first clear all states
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_HIDDEN);
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_DISABLED);
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_CHECKED);
				lv_btnmatrix_clear_btn_ctrl(obj, to_val, LV_BTNMATRIX_CTRL_CHECKABLE);
				lv_buttonmatrix_set_button_ctrl(obj,to_val, (lv_buttonmatrix_ctrl_t)until_val); // id, button_ctrl
			}
			if (strcmp(attribute_name,"width")==0) lv_buttonmatrix_set_button_width(obj,to_val, until_val); // id, width
		} else 
		if (lv_obj_get_class(obj) == &lv_textarea_class) {
			if (strcmp(attribute_name,"focused")==0) lv_obj_add_state(obj, LV_STATE_FOCUSED);
		} else
		if ((lv_obj_get_class(obj) == &lv_button_class)  || (lv_obj_get_class(obj) == &lv_label_class)) {
		   if (strstr(attribute_name,"rotation")) lv_obj_set_style_transform_angle(obj, to_val, 0);
		}
	}
}


void ui_set_style(char * obj_name, char * style_name, int to_val){
	lv_style_t* obj = style_registry.get(obj_name);
	if (obj) {
		if (strcmp(style_name,"text font")==0) lv_style_set_text_font(obj,  get_font_from_scale(to_val));
			else if (strstr(style_name,"bg color")) lv_style_set_bg_color(obj, lv_color_hex(to_val));
			else if (strstr(style_name,"bg opa")) lv_style_set_bg_opa(obj, to_val);
			else if (strstr(style_name,"border width")) lv_style_set_border_width(obj, to_val);
    		else if (strstr(style_name,"border color")) lv_style_set_border_color(obj, lv_color_hex(to_val));
			else if (strstr(style_name,"radius")) lv_style_set_radius(obj, to_val);
			else if (strstr(style_name,"shadow width")) lv_style_set_shadow_width(obj, to_val);
			else if (strstr(style_name,"shadow offset x")) lv_style_set_shadow_offset_x(obj, to_val);
			else if (strstr(style_name,"shadow offset y")) lv_style_set_shadow_offset_y(obj, to_val);
			else if (strstr(style_name,"shadow opa")) lv_style_set_shadow_opa(obj, to_val);
			else if (strstr(style_name,"width")) lv_style_set_width(obj, to_val);
			else if (strstr(style_name,"line width")) lv_style_set_line_width(obj, to_val);
			else if (strstr(style_name,"line color")) lv_style_set_line_color(obj, lv_color_hex(to_val));
			else if (strstr(style_name,"text color")) lv_style_set_text_color(obj, lv_color_hex(to_val));


			

	}
}


struct ClassNameMap {
    const lv_obj_class_t *cls;
    const char *name;
} class_map[] = {
    { &lv_buttonmatrix_class, "buttonmatrix" },
    { &lv_label_class,       "label" },
    { &lv_button_class,         "button" },
    { &lv_obj_class,         "generic_obj" }, // base class
    { nullptr,               nullptr }
};

// Get a readable class name
const char *get_class_name(const lv_obj_class_t *cls) {
    for (int i = 0; class_map[i].cls; i++) {
        if (class_map[i].cls == cls) return class_map[i].name;
    }
    return "(unknown)";
}

// Print class hierarchy
void print_class_hierarchy(const lv_obj_t *obj) {
    if (!obj) return;

    const lv_obj_class_t *cls = lv_obj_get_class(obj);

    // char s[100];
	// sprintf(s,"class name - %s (%p)", get_class_name(cls), cls);
	// outputString(s);
}

void ui_set_color(char * obj_name, int color) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_label_class) {
			lv_obj_set_style_text_color(obj, lv_color_hex(color), LV_PART_MAIN); 
		} else 
		if  (lv_obj_get_class(obj) == &lv_led_class) {
			lv_led_set_color(obj,lv_color_hex(color));
		} else
		if  (lv_obj_get_class(obj) == &lv_switch_class) {
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN  | LV_STATE_DEFAULT);
			lv_obj_set_style_bg_opa(obj, LV_OPA_COVER,LV_PART_MAIN |LV_STATE_DEFAULT);
	    } else
		if  ((lv_obj_get_class(obj) == &lv_arc_class) || (lv_obj_get_class(obj) == &lv_spinner_class)) {
			//outputString("change color arc or spinner");
			lv_obj_set_style_arc_color(obj, lv_color_hex(color), LV_PART_MAIN);
 		} else
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_MAIN);
	
	}
}

void ui_set_color_2nd(char * obj_name, int color) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_button_class) {
			lv_obj_t *label = lv_obj_get_child(obj, 0);
			lv_obj_set_style_text_color(label, lv_color_hex(color), 0); 
		} else if ((lv_obj_get_class(obj) == &lv_arc_class)  || (lv_obj_get_class(obj) == &lv_spinner_class)){
			lv_obj_set_style_arc_color(obj, lv_color_hex(color), LV_PART_INDICATOR);
		} else if (lv_obj_get_class(obj) == &lv_switch_class) {
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR|LV_STATE_CHECKED);
		    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_INDICATOR|LV_STATE_CHECKED);
		}
 		else 
			lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_INDICATOR);
		
	}
}

void ui_set_color_3rd(char * obj_name, int color) {
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		lv_obj_set_style_bg_color(obj, lv_color_hex(color), LV_PART_KNOB);
		if (lv_obj_get_class(obj) == &lv_switch_class) lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_KNOB);
	}
}

void printall() {
	registry.printall();

}


// helper code for object selection

typedef enum {
    CMD_UNKNOWN = -1,
    CMD_BUTTON,
	CMD_LABEL,
    CMD_ARC,
    CMD_SLIDER,
    CMD_LED,
    CMD_SWITCH,
	CMD_BAR,
	CMD_TABVIEW,
	CMD_TILEVIEW,
	CMD_LIST,
	CMD_ROLLER,
	CMD_SCREEN,
	CMD_STYLE,
	CMD_SPINBOX,
	CMD_SPINNER,
	CMD_SCALE,
	CMD_TEXTAREA,
	CMD_KEYBOARD,
    CMD_COUNT
} Command;


Command lookup_cmd(const char *s) {
    if (strcmp(s, "button") == 0)   return CMD_BUTTON;
	if (strcmp(s, "label") == 0)    return CMD_LABEL;
    if (strcmp(s, "arc") == 0)      return CMD_ARC;
    if (strcmp(s, "slider") == 0)   return CMD_SLIDER;
    if (strcmp(s, "led") == 0)      return CMD_LED;
    if (strcmp(s, "switch") == 0)   return CMD_SWITCH;
	if (strcmp(s, "bar") == 0)   	return CMD_BAR;
	if (strcmp(s, "tabview") == 0)  return CMD_TABVIEW;
	if (strcmp(s, "tileview") == 0) return CMD_TILEVIEW;
	if (strcmp(s, "list") == 0)     return CMD_LIST;
	if (strcmp(s, "roller") == 0)   return CMD_ROLLER;
	if (strcmp(s, "style") == 0)   return CMD_STYLE;
	if (strcmp(s, "spinbox") == 0)   return CMD_SPINBOX;
	if (strcmp(s, "spinner") == 0)   return CMD_SPINNER;
	if (strcmp(s, "screen") == 0)   return CMD_SCREEN;
	if (strcmp(s, "scale") == 0)   return CMD_SCALE;
	if (strcmp(s, "textarea") == 0)   return CMD_TEXTAREA;
	if (strcmp(s, "keyboard") == 0)   return CMD_KEYBOARD;
    return CMD_UNKNOWN;
}

static OBJ primLVGLaddfont(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[1]);
	char* filename = obj2str(args[0]);
	
	ui_add_font(obj_name,filename);
	return falseObj;
}


static OBJ primLVGLaddimg(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[1]);
	char* filename = obj2str(args[0]);
	const char *parent;
	if (argCount > 2) {
		parent = obj2str(args[2]);
	} else {
		parent = "lv_scr_act";
	}

	ui_add_image(obj_name,filename,parent);
	return falseObj;
}

// primLVGL function definitions
static OBJ primLVGLprintall(int argCount, OBJ *args) {
	printall();
	return falseObj;
}

static OBJ primLVGLgetallobjs(int argCount, OBJ *args) {
	int count = registry.size();
	count += font_buffer.size();
	count += style_registry.size();
	count += series_registry.size();
	count += btnmap_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	std::vector<std::string> names = registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = font_buffer.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = style_registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = series_registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	names = btnmap_registry.getAllNames();
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}


static OBJ primLVGLgetallfonts(int argCount, OBJ *args) {
	std::vector<std::string> names = font_buffer.getAllNames();
	int count = font_buffer.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}


static OBJ primLVGLgetallstyles(int argCount, OBJ *args) {
	std::vector<std::string> names = style_registry.getAllNames();
	int count = style_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}


static OBJ primLVGLgetallseries(int argCount, OBJ *args) {
	std::vector<std::string> names = series_registry.getAllNames();
	int count = series_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}

static OBJ primLVGLgetallbtnmaps(int argCount, OBJ *args) {
	std::vector<std::string> names = btnmap_registry.getAllNames();
	int count = btnmap_registry.size();
	OBJ result = newObj(ListType, count+1, zeroObj);
	FIELD(result, 0) = int2obj(count);
	int i=1;
	for (const auto& name : names) {
		FIELD(result, i)=newStringFromBytes(name.c_str(), name.length());
		i++;
	}
	return result;
}

static OBJ primLVGLaddBtn(int argCount, OBJ *args) {
	int scale = 1;
	char* obj_name = obj2str(args[0]);
	const char *label_text;
	if (argCount >1) {
		scale = obj2int(args[1]);
	} else scale=1;
	if (argCount >2) {
		OBJ value = args[2];
		if (IS_TYPE(value, StringType)) {
			label_text = obj2str(value);
		} else if (isInt(value)) {
   			char s[20];
   			sprintf(s, "%d", obj2int(value));
			label_text=s;
		} else
			label_text="";
	} else 	label_text = obj2str(args[0]);
	const char *parent;
	if (argCount > 3) {
		parent = obj2str(args[3]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_button_label(obj_name, scale, label_text, parent);
	return falseObj;
}

static OBJ primLVGLaddLabel(int argCount, OBJ *args) {
	int scale = 1;
	char* label_name = obj2str(args[0]);
	const char *label_text;
	if (argCount >1) {
		scale = obj2int(args[1]);
	} else scale =1;
	if (argCount >2) {
		OBJ value = args[2];
		if (IS_TYPE(value, StringType)) {
			label_text = obj2str(args[2]);
		} else if (isInt(value)) {
   			char s[20];
   			sprintf(s, "%d", obj2int(value));
			label_text=s;
		} else
			label_text="";
	} else 	label_text = obj2str(args[0]);
	const char *parent;
	if (argCount > 3) {
		parent = obj2str(args[3]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_label(label_name, scale, label_text, parent);
	return falseObj;
}

static OBJ primLVGLaddSlider(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	const char *parent;
	if (argCount > 1) {
		parent = obj2str(args[1]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_slider(obj_name, parent);
	return falseObj;
}

static OBJ primLVGLaddTab(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* parent = obj2str(args[1]);
	ui_add_tab(obj_name, parent);
	return falseObj;
}

static OBJ primLVGLaddTile(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* parent = obj2str(args[1]);
	int col_id = obj2int(args[2]);
	int row_id = obj2int(args[3]);
	uint8_t l = (trueObj == args[4]) ? 1:0;
	uint8_t r = (trueObj == args[5]) ? 1:0;
	uint8_t t = (trueObj == args[6]) ? 1:0;
	uint8_t b = (trueObj == args[7]) ? 1:0;
	lv_dir_t dir = (lv_dir_t)(l + (r<<1) + (t<<2) + (b<<3));
	ui_add_tile(obj_name, parent, col_id, row_id, dir);
	return falseObj;
}

static OBJ primLVGLaddSeries(int argCount, OBJ *args) {
	char* series = obj2str(args[0]);
	char* chart = obj2str(args[1]);
	int color = obj2int(args[2]);
	ui_add_series(series, chart, color);
	return falseObj;
}

static OBJ primLVGLaddchart(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* chart_type = obj2str(args[1]);
	char* chart_update_mode = obj2str(args[2]);
	const char* parent = "lv_scr_act";
	if (argCount > 3) {
		parent = obj2str(args[3]);
	} 
	ui_add_chart(obj_name, parent, chart_type, chart_update_mode);
	return falseObj;
}

static OBJ primLVGLaddArc(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	const char *parent;
	if (argCount > 1) {
		parent = obj2str(args[1]);
	} else {
		parent = "lv_scr_act";
	}
	ui_create_arc(obj_name, parent);
	return falseObj;
}


void free_btnmap(char **btnmap, size_t count) {
    if (!btnmap) return;
    for (size_t i = 0; i <= count; i++) {
        free(btnmap[i]);  // free each string
    }
    free(btnmap);         // free the array of pointers
}




static OBJ primLVGLaddButtonMatrix(int argCount, OBJ *args) {
	int count;
	char* obj_name = obj2str(args[0]);
	const char *parent;
	OBJ obj = args[1];
	if (argCount > 2) {
		parent = obj2str(args[2]);
	} else {
		parent = "lv_scr_act";
	}

	if (IS_TYPE(obj, ListType)) {
		count = obj2int(FIELD(obj, 0));
		if (count >= WORDS(obj)) count = WORDS(obj) - 1;
		if (!registry.get(obj_name) && registry.get(parent)) {
			// alloc array of strings 
			char** btnmap = (char **)malloc((count+1) * sizeof(char*));
			for (size_t i = 1; i < count+1; i++) {
				OBJ field =  FIELD(obj, i);
				char* string_n = obj2str(field);
				size_t len = strlen(string_n);
				//outputString(string_n);
				if (len==0) {
						btnmap[i-1] = (char *)malloc(2);
						strcpy(btnmap[i-1], "\n");
				} else {
					btnmap[i-1] = (char *)malloc(len + 1); // +1 for null terminator
					strcpy(btnmap[i-1], string_n);
				}
			}
			btnmap[count]=NULL; // end button map
			lv_obj_t* obj = lv_buttonmatrix_create(registry.get(parent));
			lv_buttonmatrix_set_map(obj, btnmap);
			lv_obj_add_event_cb(obj, ui_log_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
			registry.add(obj_name, obj);
			btnmap_registry.add(obj_name, btnmap); // store btnmap in registry
		}
	}
		
	return falseObj;
}



static OBJ primLVGLaddObject(int argCount, OBJ *args) {
	char* obj_type = obj2str(args[0]);
	char* obj_name = obj2str(args[1]);
	const char *parent;
	if (argCount > 2) {
		parent = obj2str(args[2]);
	} else {
		parent = "lv_scr_act";
	}
	//bool event = (argCount > 1) ? (trueObj == args[1]) : true;
	Command cmd = lookup_cmd(obj_type);
	switch (cmd) {
		case CMD_BUTTON:
			//outputString("Handle BUTTON");
			ui_create_button(obj_name, parent);
			break;
		case CMD_ARC:
			//outputString("Handle ARC");
			ui_create_arc(obj_name, parent);
			break;
		case CMD_SLIDER:
			//outputString("Handle SLIDER");
			ui_create_slider(obj_name, parent);
			break;
		case CMD_LED:
			//outputString("Handle LED");
			ui_create_led(obj_name, parent);
			break;
		case CMD_SWITCH:
			//outputString("Handle SWITCH");
			ui_create_switch(obj_name, parent);
			break;
		case CMD_BAR:
			//outputString("Handle BAR");
			ui_create_bar(obj_name, parent);
			break;
		case CMD_TABVIEW:
			//outputString("Handle TABVIEW");
			ui_create_tabview(obj_name, parent);
			break;
		case CMD_TILEVIEW:
			//outputString("Handle TILEVIEW");
			ui_create_tileview(obj_name, parent);
			break;
		case CMD_LIST:
			//outputString("Handle LIST");
			ui_create_list(obj_name, parent);
			break;
		case CMD_ROLLER:
			//outputString("Handle ROLLER");
			ui_create_roller(obj_name, parent);
			break;
		case CMD_SCREEN:
			//outputString("Handle SCREEN");
			ui_create_screen(obj_name, parent);
			break;
		case CMD_STYLE:
		 	//outputString("Handle STYLE");
		 	ui_create_style(obj_name, parent);
		 	break;
		case CMD_SPINBOX:
		 	//outputString("Handle SPINBOX");
		 	ui_create_spinbox(obj_name, parent);
		 	break;
		case CMD_SPINNER:
		 	//outputString("Handle SPINNER");
		 	ui_create_spinner(obj_name, parent);
		 	break;
		case CMD_SCALE:
		 	ui_create_scale(obj_name, parent);
		 	break;
		case CMD_TEXTAREA:
		 	ui_create_textarea(obj_name, parent);
		 	break;
		case CMD_KEYBOARD:
		 	ui_create_keyboard(obj_name, parent);
		 	break;
		default:
			outputString("Unknown command");;
	}
	return falseObj;
}

static OBJ primLVGLgetSymbol(int argCount, OBJ *args) {
    static const std::unordered_map<std::string, int> symbolMap = {
        {"bullet",        20042},
        {"audio",         61441},
        {"video",         61448},
        {"list",          61451},
        {"ok",            61452},
        {"close",         61453},
        {"power",         61457},
        {"settings",      61459},
        {"home",          61461},
        {"download",      61465},
        {"drive",         61468},
        {"refresh",       61473},
        {"mute",          61478},
        {"volume_mid",    61479},
        {"volume_max",    61480},
        {"image",         61502},
        {"tint",          61507},
        {"prev",          61512},
        {"play",          61515},
        {"pause",         61516},
        {"stop",          61517},
        {"next",          61521},
        {"eject",         61522},
        {"left",          61523},
        {"right",         61524},
        {"plus",          61543},
        {"minus",         61544},
        {"eye_open",      61550},
        {"eye_close",     61552},
        {"warning",       61553},
        {"shuffle",       61556},
        {"up",            61559},
        {"down",          61560},
        {"loop",          61561},
        {"directory",     61563},
        {"upload",        61587},
        {"call",          61589},
        {"cut",           61636},
        {"copy",          61637},
        {"save",          61639},
        {"bars",          61641},
        {"envelope",      61664},
        {"charge",        61671},
        {"paste",         61674},
        {"bell",          61683},
        {"keyboard",      61724},
        {"gps",           61732},
        {"file",          61787},
        {"wifi",          61931},
        {"battery_full",  62016},
        {"battery_3",     62017},
        {"battery_2",     62018},
        {"battery_1",     62019},
        {"battery_empty", 62020},
        {"usb",           62087},
        {"bluetooth",     62099},
        {"trash",         62189},
        {"edit",          62212},
        {"backspace",     62810},
        {"sd_card",       63426},
        {"new_line",      63650}
    };
	std::string symbol = obj2str(args[0]);
    auto it = symbolMap.find(symbol);
    if (it != symbolMap.end()) {
        return int2obj(it->second);
    }
    return falseObj; // not found
}

static OBJ primLVGLloadScreen(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	//if (obj) lv_scr_load_anim(obj, LV_SCR_LOAD_ANIM_FADE_IN, 200, 0, false);
	if (obj) lv_scr_load(obj);
	return falseObj;
}

static OBJ primLVGLsetParent(int argCount, OBJ *args) {
	char* obj = obj2str(args[0]);
	char* parent = obj2str(args[1]);
	// states and parts are only used when applying a style to an object]
	int states = 0;
	int parts = 0;
	if (argCount >2) {
		states = obj2int(args[2]);
	}
	if (argCount >3) {
		parts = obj2int(args[3]);
	}
	ui_set_parent(obj, parent, states, parts);
	return falseObj;
}

#if defined(COCUBE)
static OBJ primLVGLaddgroup(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	lv_group_add_obj(group, obj);
	return falseObj;
}
#endif

static OBJ primLVGLdelObj(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	ui_delete_obj(obj_name);
	return falseObj;
}

static OBJ primLVGLsetSize(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	int w = obj2int(args[1]);
	int h = obj2int(args[2]);
	ui_set_size(obj_name, w, h);
	return falseObj;
}

static OBJ primLVGLsetPos(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	int pos_x = obj2int(args[1]);
	int pos_y = obj2int(args[2]);
	ui_set_pos(obj_name, pos_x, pos_y);
	return falseObj;
}

static OBJ primLVGLsetScroll(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	char* direction = obj2str(args[1]);
	lv_dir_t scroll_dir = LV_DIR_NONE;
	if (strcmp(direction,"hor")==0) scroll_dir = LV_DIR_HOR;
	else if (strcmp(direction,"ver")==0) scroll_dir = LV_DIR_VER;
	else if (strcmp(direction,"all")==0) scroll_dir = LV_DIR_ALL;
	ui_set_scroll(obj_name, scroll_dir);
	return falseObj;
}




static OBJ primLVGLsetVal(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	int value;
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_led_class) {
			if (trueObj == args[1]) lv_led_on(obj);
			else lv_led_off(obj);
		} else if (lv_obj_get_class(obj) == &lv_switch_class) {
			if (IS_TYPE(args[1], BooleanType )) {
				if (args[1]==trueObj) value = 1; else value=0;
			} 
		}
		else
		   value = obj2int(args[1]);
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			lv_arc_set_value(obj, value);
		} else 
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			lv_slider_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_bar_class) {
			lv_bar_set_value(obj, value, LV_ANIM_OFF);
		} else
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			lv_spinbox_set_value(obj, value);
		} else
		if (lv_obj_get_class(obj) == &lv_switch_class) {
			if (value==0) lv_obj_remove_state(obj, LV_STATE_CHECKED);
			else if (value>0) lv_obj_add_state(obj, LV_STATE_CHECKED);
		}  else
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			lv_roller_set_visible_row_count(obj,value);
		} 
	}
	return falseObj;
}

static OBJ primLVGLsetnextvalue(int argCount, OBJ *args) {
	char* series = obj2str(args[0]);
	char *chart = obj2str(args[1]);
	int val = obj2int(args[2]);
	if (argCount > 3) {
		int val2 = obj2int(args[3]);
		ui_set_next_value2(series, chart, val, val2);
	} else {
		ui_set_next_value(series, chart, val);
	}
		return falseObj;
}	


static OBJ primLVGLsetText(int argCount, OBJ *args) {
	int scale = 1;
	char* obj_name = obj2str(args[0]);
	char* obj_text;
	char *font_name; 
	OBJ value = args[1];
	if (IS_TYPE(value, StringType)) {
		obj_text = obj2str(value);
	} else if (isInt(value)) {
		char s[20];
		sprintf(s, "%d", obj2int(value));
		obj_text = s;
	} else {
		char s[1];
		s[0]='\0';
		obj_text=s;
	}
	if (argCount >2) {
		if (IS_TYPE(args[2], StringType)) {
			font_name = obj2str(args[2]);
			ui_set_text_font(obj_name, obj_text, font_name);
		} else {
			scale = obj2int(args[2]);
			ui_set_text(obj_name, obj_text, scale);
		}
	} else 
		ui_set_text(obj_name, obj_text, scale);
	
	return falseObj;
}

static OBJ primLVGLsetattribute(int argCount, OBJ *args) {
	char* attribute_name = obj2str(args[0]);
	char* obj_name = obj2str(args[1]);
	int to_val = obj2int(args[2]);
	int until_val=100;	
	if (argCount >3) {
		until_val = obj2int(args[3]);
	}
	ui_set_attribute(obj_name, attribute_name, to_val, until_val);
	return falseObj;
}


static OBJ primLVGLsetstyle(int argCount, OBJ *args) {
	char* style_name = obj2str(args[0]);
	char* obj_name = obj2str(args[1]);
	int to_val = obj2int(args[2]);
	ui_set_style(obj_name, style_name, to_val);
	return falseObj;
}

static OBJ primLVGLgetVal(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	lv_obj_t* obj = registry.get(obj_name);
	if (obj) {
		if (lv_obj_get_class(obj) == &lv_arc_class) {
			return int2obj(lv_arc_get_value(obj));
 		} else 
		if (lv_obj_get_class(obj) == &lv_slider_class) {
			return int2obj(lv_slider_get_value(obj));
		} else 
		if (lv_obj_get_class(obj) == &lv_spinbox_class) {
			return int2obj(lv_spinbox_get_value(obj));
		} else 
		if (lv_obj_get_class(obj) == &lv_textarea_class) {
			const char * text = lv_textarea_get_text(obj);
			return newStringFromBytes(text, strlen(text));
		} else 
		if (lv_obj_get_class(obj) == &lv_switch_class) {
			return lv_obj_has_state(obj, LV_STATE_CHECKED)  ? trueObj : falseObj;
		} else 
		if (lv_obj_get_class(obj) == &lv_roller_class) {
			char buf[100];
			lv_roller_get_selected_str(obj, buf, sizeof(buf));
			return  newStringFromBytes(buf, strlen(buf));
		} else
		if (lv_obj_check_type(obj, &lv_buttonmatrix_class)){
			// used when event to retuen the id of the btn
			int id = lv_buttonmatrix_get_selected_button(obj);
			int checked = 0;
			if (lv_buttonmatrix_has_button_ctrl(obj, id, LV_BTNMATRIX_CTRL_CHECKED)) checked = 512;
			return int2obj(id + checked);
		} 
		else return falseObj;
	} else return falseObj;
}




static OBJ primLVGLsetColor(int argCount, OBJ *args) {
	char* obj_name = obj2str(args[0]);
	int color = obj2int(args[1]);
	ui_set_color(obj_name, color);
	if ( argCount > 2) {
		color = obj2int(args[2]);
		ui_set_color_2nd(obj_name, color);
	}
	if ( argCount > 3) {
		color = obj2int(args[3]);
		ui_set_color_3rd(obj_name, color);
	}
	
	return falseObj;
}


static OBJ primLVGLgetEvent(int argCount, OBJ *args) {
	std::string name;
	int code = ui_get_last_event(name);
	OBJ result = newStringFromBytes(name.c_str(), name.length());
	return result;
}

static OBJ primLVGLEvent(int argCount, OBJ *args) {
	bool check_event = event_seen;
	// char s[100];
	// sprintf(s,"event %d",event_seen);
	// outputString(s);
	event_seen = false; // wipe event for next event
	if (check_event) return trueObj; else return falseObj;
}

 
static OBJ primLVGLon(int argCount, OBJ *args) {
	set_lvgl(trueObj == args[0]);
	return falseObj;

}

static OBJ primLVGLtick(int argCount, OBJ *args) {
	lvgl_tick();
	return falseObj;

}

// dummy function for testing initialisation lvgl
static OBJ primLVGLinit(int argCount, OBJ *args) {
	if (useTFT) // check whether TFT is active, only then setup LVGL
		setup_lvgl();
	return falseObj;

}

static OBJ primLVGLstate(int argCount, OBJ *args) {
	char s[100];
	sprintf(s,"uselvgl: %d, lvgl_initialzed: %d\n", useLVGL, LVGL_initialized);
	outputString(s);	
	return falseObj;

}

#include "esp_heap_caps.h"

static OBJ primLVGLpsram(int argCount, OBJ *args) {
	int val = heap_caps_get_free_size(MALLOC_CAP_SPIRAM); // in bytes

	char s[100];
	// sprintf(s,"PSRAM total size: %d",ESP.getPsramSize());
	// outputString(s);	
	// sprintf(s,"PSRAM free: %d",val);
    // outputString(s);	
	// sprintf(s,"PSRAM largest free block: %d",heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
	// outputString(s);	
	sprintf(s,"RAM heap free: %d",heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    outputString(s);	
	sprintf(s,"free heap after: %d ",  ESP.getFreeHeap()); //,ESP.getFreePsram());
	outputString(s);
	return int2obj(val);
}

#if defined(LVGL_SNAPSHOT)
#include <WiFi.h>
#include <HTTPClient.h>


#define STRING_OBJ_CONST(s) \
	struct { uint32 header = HEADER(StringType, ((sizeof(s) + 4) / 4)); char body[sizeof(s)] = s; }


STRING_OBJ_CONST("Snapshot failed") statusSnapshotFailed;
STRING_OBJ_CONST("PSRAM alloc failed") statusPsramAllocFailed;



static OBJ primLVGLsnapshot(int argCount, OBJ *args) {
	char *upload_url = obj2str(args[0]);
 	lv_obj_t *scr = lv_screen_active();
    lv_coord_t w = lv_obj_get_width(scr);
    lv_coord_t h = lv_obj_get_height(scr);
    size_t buf_size = w * h * 2; // RGB565 = 2 bytes per pixel

    // Allocate snapshot buffer in PSRAM
    uint8_t *psram_buf = (uint8_t *) heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!psram_buf) {
		return (OBJ)  &statusPsramAllocFailed;
    }

    // Create LVGL image descriptor
    lv_image_dsc_t snapshot;
    lv_result_t res = lv_snapshot_take_to_buf(scr,
                                              LV_COLOR_FORMAT_NATIVE,
                                              &snapshot,
                                              psram_buf,
                                              buf_size);
    if (res != LV_RESULT_OK) {
        lv_free(psram_buf);
        return (OBJ) &statusSnapshotFailed;
    }
	char s[100];
	sprintf(s,"📸 Snapshot OK: %d x %d (%d bytes)\n", w, h, buf_size);
	outputString(s);	
    
    // --- Upload to Web Server ---
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(upload_url);
        http.addHeader("Content-Type", "application/octet-stream");

        int httpResponseCode = http.POST(psram_buf, buf_size);
        if (httpResponseCode > 0) {
            sprintf(s,"✅ Upload OK, code: %d\n", httpResponseCode);
			outputString(s);	
        } else {
			sprintf(s,"❌ Upload failed, error: %s\n", http.errorToString(httpResponseCode).c_str());
			outputString(s);			
        }
        http.end();
    } else {
         fail(noWiFi);
    }

    // Cleanup
    free(psram_buf);
  	return falseObj;

}



#endif   // LVGL_SNAPSHOT

#endif 
// Touchscreen Primitives

static OBJ primTftTouched(int argCount, OBJ *args) {
	#ifdef HAS_TOUCH_SCREEN
		return screenTouched() ? trueObj : falseObj;
	#endif
	return falseObj;
}

static OBJ primTftTouchX(int argCount, OBJ *args) {
	#ifdef HAS_TOUCH_SCREEN
		return int2obj(screenTouchX());
	#endif
	return int2obj(-1);
}

static OBJ primTftTouchY(int argCount, OBJ *args) {
	#ifdef HAS_TOUCH_SCREEN
		return int2obj(screenTouchY());
	#endif
	return int2obj(-1);
}

static OBJ primTftTouchPressure(int argCount, OBJ *args) {
	#ifdef HAS_TOUCH_SCREEN
		return int2obj(screenTouchPressure());
	#endif
	return int2obj(-1);
}

// Primitives

static PrimEntry entries[] = {
	{"setBacklight", primSetBacklight},
	{"getWidth", primGetWidth},
	{"getHeight", primGetHeight},
	{"setPixel", primSetPixel},
	{"pixelRow", primPixelRow},
	{"line", primLine},
	{"rect", primRect},
	{"roundedRect", primRoundedRect},
	{"circle", primCircle},
	{"triangle", primTriangle},
	{"text", primText},
	{"clear", primClear},
	{"deferUpdates", primDeferUpdates},
	{"resumeUpdates", primResumeUpdates},

	{"mergeBitmap", primMergeBitmap},
	{"drawBuffer", primDrawBuffer},
	{"drawBitmap", primDrawBitmap},

	{"tftTouched", primTftTouched},
	{"tftTouchX", primTftTouchX},
	{"tftTouchY", primTftTouchY},
	{"tftTouchPressure", primTftTouchPressure},

	{"aruco", primAruco},
	{"aprilTag", primAprilTag},

#if defined(LVGL) 
	{"LVGLon",primLVGLon},
//	{"LVGLbutton",primLVGLbutton},
	{"LVGLtick",primLVGLtick},
	{"LVGLstate",primLVGLstate},
	#if defined(LVGL_SNAPSHOT)
		{"LVGLsnapshot",primLVGLsnapshot},
	#endif
	{"LVGLaddbtn",primLVGLaddBtn},
	{"LVGLaddlabel",primLVGLaddLabel},
	{"LVGLaddslider",primLVGLaddSlider},
	{"LVGLaddarc",primLVGLaddArc},
	{"LVGLaddtab",primLVGLaddTab},
	{"LVGLaddseries",primLVGLaddSeries},
	{"LVGLaddchart",primLVGLaddchart},
	{"LVGLsetnextvalue",primLVGLsetnextvalue},
	{"LVGLaddtile",primLVGLaddTile},
	{"LVGLaddbuttonmatrix",primLVGLaddButtonMatrix},
	{"LVGLaddobj",primLVGLaddObject},
	{"LVGLdelobj",primLVGLdelObj},
	{"LVGLsetparent",primLVGLsetParent},
	{"LVGLsetpos",primLVGLsetPos},
	{"LVGLsetsize",primLVGLsetSize},
	{"LVGLsetval", primLVGLsetVal},
	{"LVGLsettext",primLVGLsetText},
	{"LVGLsetattribute",primLVGLsetattribute},
	{"LVGLsetstyle",primLVGLsetstyle},
	{"LVGLgetval", primLVGLgetVal},
	{"LVGLloadscreen",primLVGLloadScreen},
	{"LVGLevent",primLVGLEvent},
	{"LVGLgetevent",primLVGLgetEvent},
	{"LVGLsetcolor", primLVGLsetColor},
	{"LVGLgetallobjs", primLVGLgetallobjs},
	{"LVGLgetallfonts",primLVGLgetallfonts},
	{"LVGLgetallstyles",primLVGLgetallstyles},
	{"LVGLgetallseries",primLVGLgetallseries},
	{"LVGLgetallbtnmaps",primLVGLgetallbtnmaps},
	{"LVGLgetsymbol",primLVGLgetSymbol},
	{"LVGLinit", primLVGLinit},
	{"LVGLaddimg", primLVGLaddimg},
 	{"LVGLaddfont",primLVGLaddfont},
	{"LVGLsetscroll",primLVGLsetScroll},
	{"LVGLpsram",primLVGLpsram},
	#if (defined(LMSDIAPLY) && defined(BREAKOUT))||defined(CYDROT)
		{"fliptouch",primfliptouch},
	#endif
	#if defined(COCUBE)
		{"LVGLaddgroup",primLVGLaddgroup},
	#endif

#endif

};

void addTFTPrims() {
	addPrimitiveSet(TFTPrims, "tft", sizeof(entries) / sizeof(PrimEntry), entries);
}
