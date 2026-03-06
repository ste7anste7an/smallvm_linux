/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2018 John Maloney, Bernat Romagosa, and Jens Mönig

// linux.c - Microblocks for Linux
// An adaptation of raspberryPi.c by John Maloney to
// work on a GNU/Linux TTY as a serial device

// John Maloney, December 2017
// Bernat Romagosa, February 2018

#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h> // still needed?
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h> // still needed?
#include <termios.h>
#include <unistd.h>
#include <signal.h>


/* linux.c — serial routines using /dev/ttyUSB0 @ 115200 (8N1), non-blocking
 *
 * Call serial_init() once at startup (it opens/configures the port).
 * Then use recvBytes/canReadByte/sendByte/sendBytes as before.
 *
 * Uses an internal 256-byte RX buffer.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#ifndef uint8
typedef uint8_t uint8;
#endif

static int serial_fd = -1;

/* Internal 256-byte receive buffer */
static uint8 rx_buf[256];
static int   rx_len = 0;
static int   rx_pos = 0;

static speed_t baud_to_termios(int baud)
{
    switch(baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 460800: return B460800;
        case 921600: return B921600;
        default: return 0;
    }
}

/* Open and configure /dev/ttyUSB0 @ 115200, 8N1, raw, no flow control.
 * Returns 0 on success, -1 on failure.
 */
int serial_init(void)
{
    const char *dev = "/dev/ttyUSB0";
    const int baud = 115200;

    serial_fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if(serial_fd < 0) {
        perror("open(/dev/ttyUSB0)");
        return -1;
    }

    struct termios tio;
    if(tcgetattr(serial_fd, &tio) != 0) {
        perror("tcgetattr");
        close(serial_fd);
        serial_fd = -1;
        return -1;
    }

    cfmakeraw(&tio);

    /* 8N1 */
    tio.c_cflag &= ~PARENB;
    tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE;
    tio.c_cflag |= CS8;

    /* Enable receiver, ignore modem control lines */
    tio.c_cflag |= (CLOCAL | CREAD);

    /* No HW flow control */
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;
#endif
    tio.c_cflag &= ~HUPCL;
    /* No SW flow control */
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);

    speed_t spd = baud_to_termios(baud);
    if(spd == 0) {
        fprintf(stderr, "Unsupported baud rate: %d\n", baud);
        close(serial_fd);
        serial_fd = -1;
        return -1;
    }

    cfsetispeed(&tio, spd);
    cfsetospeed(&tio, spd);

    /* Non-blocking behavior; reads return immediately */
    tio.c_cc[VMIN]  = 0;
    tio.c_cc[VTIME] = 0;

    if(tcsetattr(serial_fd, TCSANOW, &tio) != 0) {
        perror("tcsetattr");
        close(serial_fd);
        serial_fd = -1;
        return -1;
    }

    tcflush(serial_fd, TCIOFLUSH);

    /* reset rx buffer */
    rx_len = 0;
    rx_pos = 0;

    return 0;
}

static int refill_rx(void)
{
    rx_len = 0;
    rx_pos = 0;

    for(;;) {
        ssize_t n = read(serial_fd, rx_buf, sizeof(rx_buf));
        if(n > 0) { rx_len = (int)n; return rx_len; }
        if(n == 0) return 0;
        if(errno == EINTR) continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return 0; /* treat other errors as no data (or handle/log) */
    }
}

/* Receive up to `count` bytes into `buf`.
 * Returns number of bytes copied (0 if none available right now).
 */
int recvBytes(uint8 *buf, int count)
{
    if(serial_fd < 0 || !buf || count <= 0) return 0;

    int copied = 0;

    while(copied < count) {
        if(rx_pos >= rx_len) {
            if(refill_rx() == 0) break;
        }

        int avail = rx_len - rx_pos;
        int need  = count - copied;
        int ncopy = (avail < need) ? avail : need;

        memcpy(buf + copied, rx_buf + rx_pos, (size_t)ncopy);
        rx_pos += ncopy;
        copied += ncopy;
    }

    return copied;
}

/* True if at least one byte can be read without blocking.
 * Checks internal buffer first, then kernel FIONREAD.
 */
int canReadByte(void)
{
    if(serial_fd < 0) return 0;
    if(rx_pos < rx_len) return 1;

    int bytesAvailable = 0;
    if(ioctl(serial_fd, FIONREAD, &bytesAvailable) != 0) return 0;
    return (bytesAvailable > 0);
}

/* Send exactly one byte.
 * Returns 1 on success, 0 if would-block, -1 on error.
 */
int sendByte(char aByte)
{
    if(serial_fd < 0) return -1;

    for(;;) {
        ssize_t w = write(serial_fd, &aByte, 1);
        if(w == 1) return 1;
        if(w < 0 && errno == EINTR) continue;
        if(w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return -1;
    }
}

/* Send bytes buf[start..end-1].
 * Returns number of bytes written, 0 if would-block, -1 on error.
 */
int sendBytes(uint8 *buf, int start, int end)
{
    if(serial_fd < 0) return -1;
    if(!buf) return -1;
    if(start < 0) start = 0;
    if(end < start) return 0;

    size_t len = (size_t)(end - start);
    const uint8 *p = &buf[start];

    for(;;) {
        ssize_t w = write(serial_fd, p, len);
        if(w >= 0) return (int)w;
        if(errno == EINTR) continue;
        if(errno == EAGAIN || errno == EWOULDBLOCK) return 0;
        return -1;
    }
}

/* Optional: call on shutdown */
void serial_close(void)
{
    if(serial_fd >= 0) {
        close(serial_fd);
        serial_fd = -1;
    }
}


#ifdef ARDUINO_RASPBERRY_PI
#include <wiringPi.h>
#include <wiringSerial.h>
#endif

#include "mem.h"
#include "interp.h"
#include "persist.h"

// Keyboard
int KEY_SCANCODE[255];

// Timing Functions

static int startSecs = 0;

static void initTimers() {
	struct timeval now;
	gettimeofday(&now, NULL);
	startSecs = now.tv_sec;
}

uint32 microsecs() {
	struct timeval now;
	gettimeofday(&now, NULL);

	return (1000000 * (now.tv_sec - startSecs)) + now.tv_usec;
}

uint32 millisecs() {
	struct timeval now;
	gettimeofday(&now, NULL);

	return (1000 * (now.tv_sec - startSecs)) + (now.tv_usec / 1000);
}

#ifndef ARDUINO_RASPBERRY_PI
void delay(int ms) {
	clock_t start = millisecs();
	while (millisecs() < start + ms);
}
#endif


// Communication/System Functions

int serialConnected() { return serial_fd >= 0; }

static void exitGracefully() {
	remove("/tmp/ublocksptyname");
	exit(0);
}


// System Functions

const char * boardType() {
#ifdef ARDUINO_RASPBERRY_PI
	return "Raspberry Pi";
#else
	return "Linux";
#endif
}

#ifdef ARDUINO_RASPBERRY_PI
// General Purpose I/O Pins

#define DIGITAL_PINS 32
#define ANALOG_PINS 0
#define TOTAL_PINS (DIGITAL_PINS + ANALOG_PINS)
#define PIN_LED 0

// Pin Modes

// To speed up pin I/O, the current pin input/output mode is recorded in the currentMode[]
// array to avoid calling pinMode() unless the pin mode has actually changed.

static char currentMode[TOTAL_PINS];

#define MODE_NOT_SET (-1)

#define SET_MODE(pin, newMode) { \
	if ((newMode) != currentMode[pin]) { \
		pinMode((pin), newMode); \
		currentMode[pin] = newMode; \
	} \
}

static void initPins(void) {
	// Initialize currentMode to MODE_NOT_SET (neigher INPUT nor OUTPUT)
	// to force the pin's mode to be set on first use.

	for (int i = 0; i < TOTAL_PINS; i++) currentMode[i] = MODE_NOT_SET;
}

// Pin IO Primitives

OBJ primAnalogPins(OBJ *args) { return int2obj(ANALOG_PINS); }
OBJ primDigitalPins(OBJ *args) { return int2obj(DIGITAL_PINS); }

OBJ primAnalogRead(int argCount, OBJ *args) { return int2obj(0); } // no analog inputs
void primAnalogWrite(OBJ *args) { } // analog output is not supported

OBJ primDigitalRead(int argCount, OBJ *args) {
	int pinNum = obj2int(args[0]);
	if ((pinNum < 0) || (pinNum >= TOTAL_PINS)) return falseObj;
	SET_MODE(pinNum, INPUT);
	return (HIGH == digitalRead(pinNum)) ? trueObj : falseObj;
}

void primDigitalWrite(OBJ *args) {
	int pinNum = obj2int(args[0]);
	int value = (args[1] == trueObj) ? HIGH : LOW;
	primDigitalSet(pinNum, value);
}

void primDigitalSet(int pinNum, int flag) {
	if ((pinNum < 0) || (pinNum >= TOTAL_PINS)) return;
	SET_MODE(pinNum, OUTPUT);
	digitalWrite(pinNum, flag);
};

#else // Regular Linux system (not a Raspberry Pi)

// Stubs for IO primitives

OBJ primAnalogPins(OBJ *args) { return int2obj(0); }
OBJ primDigitalPins(OBJ *args) { return int2obj(0); }
OBJ primAnalogRead(int argCount, OBJ *args) { return int2obj(0); }
void primAnalogWrite(OBJ *args) { }
OBJ primDigitalRead(int argCount, OBJ *args) { return int2obj(0); }
void primDigitalWrite(OBJ *args) { }
void primDigitalSet(int pinNum, int flag) { };
#endif

// Stubs for other functions not used on Linux

void addSerialPrims() {}
void addHIDPrims() {}
void addOneWirePrims() {}
void processFileMessage(int msgType, int dataSize, char *data) {}
void resetServos() {}
void stopPWM() {}
void systemReset() {}
void turnOffPins() {}
void stopServos() {}


// MB
//void tftSetHugePixel(int x, int y, int value) {};
//void tftSetHugePixelBits(int bits) {};
int ideConnected() {return 1;};

//void stopTone() {};
//void primSetUserLED(OBJ *args) {};
void resetRadio() { };
void BLE_setEnabled(int enableFlag) {};

char BLE_ThreeLetterID[4];

// Persistence support


char *codeFileName = "ublockscode";
FILE *codeFile;

int initCodeFile(uint8 *flash, int flashByteCount) {
	codeFile = fopen(codeFileName, "ab+");
	fseek(codeFile, 0 , SEEK_END);
	long fileSize = ftell(codeFile);

	// read code file into simulated Flash:
	fseek(codeFile, 0L, SEEK_SET);
	long bytesRead = fread((char*) flash, 1, flashByteCount, codeFile);
	if (bytesRead != fileSize) {
		outputString("initCodeFile did not read entire file");
		return -1; // error?
	}
	return bytesRead;
}

void writeCodeFile(uint8 *code, int byteCount) {
	fwrite(code, 1, byteCount, codeFile);
	fflush(codeFile);
}

void writeCodeFileWord(int word) {
	fwrite(&word, 1, 4, codeFile);
	fflush(codeFile);
}

void clearCodeFile(int ignore) {
	fclose(codeFile);
	remove(codeFileName);
	codeFile = fopen(codeFileName, "ab+");
	uint32 cycleCount = ('S' << 24) | 1; // Header record, version 1
	fwrite((uint8 *) &cycleCount, 1, 4, codeFile);
}

// Debug

void segfault() {
	printf("-- VM crashed --\n");
	exitGracefully();
}

// Linux Main

int main(int argc, char *argv[]) {
	codeFileName = "ublockscode"; // to do: allow code file name from command line

	if (argc > 1) {
		codeFileName = argv[1];
		printf("codeFileName: %s\n", codeFileName);
	}
	signal(SIGSEGV, segfault);
	signal(SIGINT, exit);
	atexit(exitGracefully);


        if(serial_init() != 0) {
           fprintf(stderr, "serial_init failed\n");
           return 1;
        }

#ifdef ARDUINO_RASPBERRY_PI
	wiringPiSetup();
	initPins();
#endif
	initTimers();
	memInit(10000); // 10k words = 40k bytes
	primsInit();
	outputString("Welcome to uBlocks for Linux!");
	restoreScripts();
	startAll();
	vmLoop();
	return 0;
}
