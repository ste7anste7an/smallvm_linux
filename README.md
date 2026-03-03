# VM on Linux

## overall

Commented out all referenced to SDL (includes, calls, etc)

### buildVM.sh

```

gcc -std=c99 -Wall -Wno-unused-variable -Wno-unused-result -O3 \
	-D GNUBLOCKS \
	-I ../vm \
	linux.c ../vm/*.c \
	linuxFilePrims.c linuxNetPrims.c \
	linuxOutputPrims.c linuxSensorPrims.c  \
	linuxIOPrims.c linuxTftPrims.c \
	-L/usr/lib/i386-linux-gnu \
	-ldl -lm -lpthread \
	-o vm_linux_arm

```

### linux.c
added
```
int sendBytes(uint8 *buf, int start, int end) {
	// sodb

	// return write(pty, &aByte, 1);
	 return write(pty, &buf[start], end - start);
}


int ideConnected() {return 1;};

//void stopTone() {};
//void primSetUserLED(OBJ *args) {};
void resetRadio() { };
void BLE_setEnabled(int enableFlag) {};

char BLE_ThreeLetterID[4];
```

chaged `void` to `int`
```
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
```

### linux.h
Changed from `int` to `unsiged long`
```
void delay(unsigned long ms);
```
### LinuxFilePrims.c
```
void addFilePrims() {
	addPrimitiveSet(FilePrims,"file", sizeof(entries) / sizeof(PrimEntry), entries);
}
```


### LinuxIOPrims.c
Added
```
static uint32 microsecondHighBits = 0;
static uint32 lastMicrosecs = 0;

uint64 totalMicrosecs() {return 0;};
	// Returns a 64-bit integer containing microseconds since start.

void handleMicosecondClockWrap() {
	// Increment microsecondHighBits if the microsecond clock has wrapped since the last
	// time this function was called.

	uint32 now = 0; //microsecs();
	if (lastMicrosecs > now) microsecondHighBits++; // clock wrapped
	lastMicrosecs = now;
}
```
Commented out whole function:
```
void audio_callback(void *user_data, Uint8 *raw_buffer, int bytes) {
```
Replaced initAudio() with
```
static void initAudio() {} 
```

changed:
```
void addIOPrims() {
	addPrimitiveSet(IOPrims,"io", sizeof(entries) / sizeof(PrimEntry), entries);
}
```

### LinuxNetPrims
replaced:
```
void addNetPrims() {
	addPrimitiveSet(NetPrims, "net", sizeof(entries) / sizeof(PrimEntry), entries);
}
```

### LinuxOutputPrims.c
replaced:
```
void addDisplayPrims() {
	addPrimitiveSet(DisplayPrims, "display", sizeof(entries) / sizeof(PrimEntry), entries);
}
```

### LinuxSensorPrims
replaced
```
void addSensorPrims() {
	addPrimitiveSet(SensorPrims, "sensors", sizeof(entries) / sizeof(PrimEntry), entries);
}
```
Commented out all references to SDL, sometimes leaving function empty.

Replaced:
```
void addTFTPrims() {
	addPrimitiveSet(TFTPrims, "tft", sizeof(entries) / sizeof(PrimEntry), entries);
}

```