/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2025 John Maloney, Bernat Romagosa, and Jens Mönig

// sdCardPrims.c - SD Card file system.
// John Maloney, July 2025

#include "mem.h"
#include "interp.h"

#if (defined(ARDUINO_ARCH_ESP32) || defined(ARDUINO_ARCH_RP2040)) && !defined(NO_SD_CARD)
	#define SD_CARD 1
#endif

#if defined(SD_CARD)

#if defined(ARDUINO_BBC_MICROBIT_V2) || defined(ARDUINO_CALLIOPE_MINI_V3)
	// SS must defined before including SdFat.h
	#define SS 16
#endif

#define USE_UTF8_LONG_NAMES 1
#define DISABLE_FS_H_WARNING 1
#include <SdFat.h>

SdFat SD;

#if defined(ARDUINO_ARCH_RP2040) // this includes RP2350
	#define SPI_SPEED SD_SCK_MHZ(12)
#else
	#define SPI_SPEED SD_SCK_MHZ(24)
#endif

#if defined(ARDUINO_ARCH_RP2040)
	#define DEFAULT_CS_PIN PIN_SPI0_SS
#elif defined(ARDUINO_M5Stack_Core_ESP32) || defined(ARDUINO_M5STACK_Core2) || defined(ARDUINO_M5STACK_CORES3)
	#define DEFAULT_CS_PIN 4
#else
	#define DEFAULT_CS_PIN SS
#endif

// Variables

#define MAX_FILE_PATH 128
#define FILE_ENTRIES 4

// Current chip select pin; -1 if not yet initialized
static int sdCardCSPin = -1;
static char fullPath[MAX_FILE_PATH]; // used to prefix "/" to file names

typedef struct {
	char fileName[MAX_FILE_PATH];
	FsFile file;
} FileEntry;

static FileEntry fileEntry[FILE_ENTRIES]; // fileEntry[] records open files

// Helper functions

static void initSDCard(int chipSelectPin) {
	if (sdCardCSPin != chipSelectPin) {
		if (sdCardCSPin != -1) SD.end();
		if (chipSelectPin < 0) chipSelectPin = DEFAULT_CS_PIN;
		int ok = SD.begin(chipSelectPin, SPI_SPEED);
		if (!ok) {
			outputString("Could not open SD Card.");
			outputString("Check wiring, chip select pin, and that card is inserted.");
			sdCardCSPin = -1;
			return;
		}
		outputString("SD Card opened");
		sdCardCSPin = chipSelectPin;
	}
}

static char *extractFilename(OBJ obj) {
	fullPath[0] = '\0';
	if (IS_TYPE(obj, StringType)) {
		char *fileName = obj2str(obj);
		if (strcmp(fileName, "ublockscode") == 0) return fullPath;
		if ('/' == fileName[0]) return fileName; // fileName already had a leading "/"
		snprintf(fullPath, MAX_FILE_PATH - 1, "/%s", fileName);
	} else {
		fail(needsStringError);
	}
	return fullPath;
}

static int entryFor(char *fileName) {
	// Return the index of a file entry for the file with the given path.
	// Return -1 if fileName doesn't match any entry.

	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	if (!fileName[0]) return -1; // empty string is not a valid file name
	for (int i = 0; i < FILE_ENTRIES; i++) {
		if (0 == strcmp(fileName, fileEntry[i].fileName)) return i;
	}
	return -1;
}

static int freeEntry() {
	// Return the index of an unused file entry or -1 if there isn't one.

	for (int i = 0; i < FILE_ENTRIES; i++) {
		if (!fileEntry[i].file) return i;
	}
	return -1; // no free entry
}

static void closeIfOpen(char *fileName) {
	// Called from fileTransfer.cpp.

	int i = entryFor(fileName);
	if (i >= 0) {
		fileEntry[i].fileName[0] = '\0';
		if (fileEntry[i].file.isOpen()) fileEntry[i].file.close();
	}
}

// Initialize

static OBJ primInit(int argCount, OBJ *args) {
	int csPin = ((argCount > 0) && isInt(args[0])) ? obj2int(args[0]) : -1;
	initSDCard(csPin);
	return falseObj;
}

// Open, Close, Delete

static OBJ primOpen(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *fileName = extractFilename(args[0]);
	if (!fileName[0]) return falseObj;

	int i = entryFor(fileName);
	if (i >= 0) { // use the existing entry
		fileEntry[i].file.seekSet(0); // read from start of file
		return falseObj;
	}

	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	i = freeEntry();
	if (i >= 0) { // initialize new entry
		fileEntry[i].fileName[0] = '\0';
		strncat(fileEntry[i].fileName, fileName, MAX_FILE_PATH - 1);
		fileEntry[i].file.open(fileName, O_RDWR | O_CREAT);
		fileEntry[i].file.seekSet(0); // read from start of file
	} else {
		outputString("File not found");
	}
	return falseObj;
}

static OBJ primClose(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *fileName = extractFilename(args[0]);

	closeIfOpen(fileName);
	return falseObj;
}

static OBJ primDelete(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *fileName = extractFilename(args[0]);
	if (!fileName[0]) return falseObj;

	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	closeIfOpen(fileName);
	SD.remove(fileName);
	return falseObj;
}

// Reading

static OBJ primEndOfFile(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *fileName = extractFilename(args[0]);

	int i = entryFor(fileName);
	if (i < 0) return trueObj;

	processMessage();
	return (!fileEntry[i].file.available()) ? trueObj : falseObj;
}

static OBJ primReadLine(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *fileName = extractFilename(args[0]);

	int i = entryFor(fileName);
	if (i < 0) return newString(0);

	char buf[1024];
	uint32 byteCount = 0;
	while ((byteCount < sizeof(buf)) && fileEntry[i].file.available()) {
		int ch = fileEntry[i].file.read();
		if ((10 == ch) || (13 == ch)) {
			if ((10 == ch) && (13 == fileEntry[i].file.peek())) fileEntry[i].file.read(); // lf-cr ending
			if ((13 == ch) && (10 == fileEntry[i].file.peek())) fileEntry[i].file.read(); // cr-lf ending
			break;
		}
		buf[byteCount++] = ch;
	}
	OBJ result = newString(byteCount);
	if (result) {
		memcpy(obj2str(result), buf, byteCount);
	}
	processMessage();
	return result;
}

static OBJ primReadBytes(int argCount, OBJ *args) {
	if (argCount < 2) return fail(notEnoughArguments);
	if (!isInt(args[0])) return fail(needsIntegerError);
	uint32 byteCount = obj2int(args[0]);
	char *fileName = extractFilename(args[1]);

	int i = entryFor(fileName);
	if (i >= 0) {
		uint8 buf[1024];
		if (byteCount > sizeof(buf)) byteCount = sizeof(buf);
		if ((argCount > 2) && isInt(args[2])) {
			fileEntry[i].file.seekSet(obj2int(args[2]));
		}
		byteCount = fileEntry[i].file.read(buf, byteCount);
		if (!byteCount && fileEntry[i].file.available()) {
			// workaround for rare read error -- skip to the next block
			int pos = fileEntry[i].file.position();
			reportNum("skipping bad file block at", pos);
			fileEntry[i].file.seekSet(pos + 256);
			byteCount = fileEntry[i].file.read(buf, byteCount);
		}
		int wordCount = (byteCount + 3) / 4;
		OBJ result = newObj(ByteArrayType, wordCount, falseObj);
		if (result) {
			setByteCountAdjust(result, byteCount);
			memcpy(&FIELD(result, 0), buf, byteCount);
			return result;
		}
	}
	processMessage();
	return newObj(ByteArrayType, 0, falseObj); // empty byte array
}

static OBJ primReadInto(int argCount, OBJ *args) {
	if (argCount < 2) return fail(notEnoughArguments);
	OBJ buf = args[0];
	char *fileName = extractFilename(args[1]);
	if (ByteArrayType != objType(buf)) return fail(needsByteArray);

	int i = entryFor(fileName);
	if (i < 0) return zeroObj; // file not found

	int bytesRead = fileEntry[i].file.read((uint8 *) &FIELD(buf, 0), BYTES(buf));
	return int2obj(bytesRead);
}

// Read positioning

static OBJ primReadPosition(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	if (!IS_TYPE(args[0], StringType)) return fail(needsStringError);
	char *fileName = extractFilename(args[0]);

	int result = 0;
	int i = entryFor(fileName);
	if (i >= 0) {
		result = fileEntry[i].file.position();
	}
	processMessage();
	return int2obj(result);
}

static OBJ primSetReadPosition(int argCount, OBJ *args) {
	if (argCount < 2) return fail(notEnoughArguments);
	if (!IS_TYPE(args[1], StringType)) return fail(needsStringError);
	char *fileName = extractFilename(args[1]);
	int newPosition = evalInt(args[0]);
	if (newPosition < 0) newPosition = 0;

	int i = entryFor(fileName);
	if (i >= 0) {
		int fileSize = fileEntry[i].file.size();
		if (newPosition > fileSize) newPosition = fileSize;
		fileEntry[i].file.seekSet(newPosition);
	}
	return falseObj;
}

// Writing

static void writeObj(int fileEntryIndex, OBJ arg) {
	int i = fileEntryIndex;
	if (IS_TYPE(arg, StringType)) {
		char *str = obj2str(arg);
		fileEntry[i].file.write(str, strlen(str));
	} else if (isInt(arg)) {
		char s[16];
		sprintf(s, "%d", obj2int(arg));
		fileEntry[i].file.write(s, strlen(s));
	} else if (isBoolean(arg)) {
		if (trueObj == arg) {
			fileEntry[i].file.write("true", 4);
		} else {
			fileEntry[i].file.write("false", 5);
		}
	} else if (IS_TYPE(arg, ListType)) {
		fileEntry[i].file.write("<List>", 6);
	} else if (IS_TYPE(arg, ByteArrayType)) {
		fileEntry[i].file.write("<ByteArray>", 11);
	}
}

static OBJ primAppendLine(int argCount, OBJ *args) {
	// Append a String to a file followed by a newline.

	if (argCount < 2) return fail(notEnoughArguments);
	if (!IS_TYPE(args[1], StringType)) return fail(needsStringError);
	char *fileName = extractFilename(args[1]);
	OBJ arg = args[0];

	int i = entryFor(fileName);
	if (i < 0) return falseObj;

	if ((i >= 0) && fileEntry[i].file)  {
		int oldPos = fileEntry[i].file.position();
		int oldSize = fileEntry[i].file.size();
		if (oldPos != oldSize) fileEntry[i].file.seekEnd(); // seek to current end
		if (IS_TYPE(arg, ListType)) {
			int count = obj2int(FIELD(arg, 0));
			for (int j = 1; j <= count; j++) {
				writeObj(i, FIELD(arg, j));
				if (j < count) fileEntry[i].file.write(", ", 2);
			}
		} else {
			writeObj(i, arg);
		}
		fileEntry[i].file.write(10); // newline
		fileEntry[i].file.flush();
		fileEntry[i].file.seekSet(oldPos); // reset position for reading
	}
	processMessage();
	return falseObj;
}

static OBJ primAppendBytes(int argCount, OBJ *args) {
	// Append a ByteArray or String to a file. No newline is added.

	if (argCount < 2) return fail(notEnoughArguments);
	OBJ data = args[0];
	char *fileName = extractFilename(args[1]);

	int i = entryFor(fileName);
	if (i < 0) return falseObj;

	int oldPos = fileEntry[i].file.position();
	int oldSize = fileEntry[i].file.size();
	if (oldPos != oldSize) fileEntry[i].file.seekEnd(); // seek to current end

	if (IS_TYPE(data, ByteArrayType)) {
		fileEntry[i].file.write((uint8 *) &FIELD(data, 0), BYTES(data));
	} else if (IS_TYPE(data, StringType)) {
		char *s = obj2str(data);
		fileEntry[i].file.write((uint8 *) s, strlen(s));
	}
	fileEntry[i].file.flush();
	fileEntry[i].file.seekSet(oldPos); // reset position for reading

	processMessage();
	return falseObj;
}

// File info

static OBJ primHasCard(int argCount, OBJ *args) {
	int oldCardPin = sdCardCSPin;
	sdCardCSPin = -999; // force re-initialization
	initSDCard(oldCardPin);
	return (sdCardCSPin >= 0) ? trueObj : falseObj;
}

static OBJ primFileExists(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *fileName = extractFilename(args[0]);
	if (!fileName[0]) return falseObj;

	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	return (SD.exists(fileName)) ? trueObj : falseObj;
}

static OBJ primFileSize(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *fileName = extractFilename(args[0]);
	if (!fileName[0]) return int2obj(-1);

	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	FsFile file = SD.open(fileName, O_RDONLY);
	if (!file) return int2obj(-1);
	int size = file.size();
	file.close();
	return int2obj(size);
}

// File and Folder listing

static FsFile listDir;

static OBJ primStartFileList(int argCount, OBJ *args) {
	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	if ((argCount > 0) && (IS_TYPE(args[0], StringType)) && (strlen(obj2str(args[0])) > 0)) {
		listDir.open(obj2str(args[0]));
	} else {
		listDir.open("/");
	}
	return falseObj;
}

static OBJ primNextFileInList(int argCount, OBJ *args) {
	char fileName[100];
	int listFolders = ((argCount > 0) && (args[0] == trueObj));
	FsFile file = listDir.openNextFile();
	if (listFolders) {
		while (file && !file.isDir()) { // skip non-foldres
			file = listDir.openNextFile();
		}
	} else {
		while (file && file.isDir()) { // skip folders
			file = listDir.openNextFile();
		}
	}
	file.getName(fileName, sizeof(fileName) - 1);
	return newStringFromBytes(fileName, strlen(fileName));
}

// Folder create/delete

static OBJ primCreateFolder(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *folderPath = extractFilename(args[0]);
	if (!folderPath[0]) return int2obj(-1);

	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	int ok = SD.mkdir(folderPath, true);
	if (!ok) outputString("Create folder failed");
	return ok ? trueObj : falseObj;
}

static OBJ primDeleteFolder(int argCount, OBJ *args) {
	if (argCount < 1) return fail(notEnoughArguments);
	char *folderPath = extractFilename(args[0]);
	if (!folderPath[0]) return int2obj(-1);

	if (sdCardCSPin < 0) initSDCard(DEFAULT_CS_PIN);
	int ok = SD.rmdir(folderPath);
	if (!ok) outputString("Delete folder failed");
	return ok ? trueObj : falseObj;
}

#endif

// Primitives

static PrimEntry entries[] = {
	#if defined(SD_CARD)
		{"init", primInit},
		{"open", primOpen},
		{"close", primClose},
		{"delete", primDelete},
		{"endOfFile", primEndOfFile},
		{"readLine", primReadLine},
		{"readBytes", primReadBytes},
		{"readInto", primReadInto},
		{"readPosition", primReadPosition},
		{"setReadPosition", primSetReadPosition},
		{"appendLine", primAppendLine},
		{"appendBytes", primAppendBytes},
		{"hasCard", primHasCard},
		{"fileExists", primFileExists},
		{"fileSize", primFileSize},
		{"startList", primStartFileList},
		{"nextInList", primNextFileInList},
		{"createFolder", primCreateFolder},
		{"deleteFolder", primDeleteFolder},
	#endif
};

void addSDCardPrims() {
	addPrimitiveSet(SDCardPrims, "sd", sizeof(entries) / sizeof(PrimEntry), entries);
}
