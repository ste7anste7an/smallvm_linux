/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

// Copyright 2025 Ilya Zverev and John Maloney

// cp437.cpp - Conversion table from Unicode to CP437
// Based on https://en.wikipedia.org/wiki/Code_page_437#Character_set
// Ilya Zverev, May 2025

#include <stdint.h>
#include "mem.h"
#include "interp.h"

// Convert a string from UTF-8 (which all MicroBlock strings are encoded in)
// to CP 437 (which is the built-in Adafruit 8-bit font codepage).
// Writes up to dstSize 8-bit codepage indices into dst and returns
// the number of indices written.

int UTF8ToCP437(char* src, char* dst, int dstSize) {
	int count = countUTF8(src);
	if (count > dstSize) count = dstSize;

	for (int i = 0; i < count; i++) {
		uint32_t code = unicodeCodePoint(src);
		if (code <= 0x7F) {
			*dst = code; // 7-bit ASCII range; no translation needed
		} else switch(code) {
			case 0x263A: *dst = 0x01; break;
			case 0x263B: *dst = 0x02; break;
			case 0x2665: *dst = 0x03; break;
			case 0x2666: *dst = 0x04; break;
			case 0x2663: *dst = 0x05; break;
			case 0x2660: *dst = 0x06; break;
			case 0x2022: *dst = 0x07; break;
			case 0x25D8: *dst = 0x08; break;
			case 0x25CB: *dst = 0x09; break;
			// case 0x25D9: *dst = 0x0A; break;
			case 0x2642: *dst = 0x0B; break;
			case 0x2640: *dst = 0x0C; break;
			// case 0x266A: *dst = 0x0D; break;
			case 0x266B: *dst = 0x0E; break;
			case 0x263C: *dst = 0x0F; break;
			case 0x25BA: *dst = 0x10; break;
			case 0x25C4: *dst = 0x11; break;
			case 0x2195: *dst = 0x12; break;
			case 0x203C: *dst = 0x13; break;
			case 0x00B6: *dst = 0x14; break;
			case 0x00A7: *dst = 0x15; break;
			case 0x25AC: *dst = 0x16; break;
			case 0x21A8: *dst = 0x17; break;
			case 0x2191: *dst = 0x18; break;
			case 0x2193: *dst = 0x19; break;
			case 0x2192: *dst = 0x1A; break;
			case 0x2190: *dst = 0x1B; break;
			case 0x221F: *dst = 0x1C; break;
			case 0x2194: *dst = 0x1D; break;
			case 0x25B2: *dst = 0x1E; break;
			case 0x25BC: *dst = 0x1F; break;
			case 0x2302: *dst = 0x7F; break;
			case 0x00C7: *dst = 0x80; break;
			case 0x00FC: *dst = 0x81; break;
			case 0x00E9: *dst = 0x82; break;
			case 0x00E2: *dst = 0x83; break;
			case 0x00E4: *dst = 0x84; break;
			case 0x00E0: *dst = 0x85; break;
			case 0x00E5: *dst = 0x86; break;
			case 0x00E7: *dst = 0x87; break;
			case 0x00EA: *dst = 0x88; break;
			case 0x00EB: *dst = 0x89; break;
			case 0x00E8: *dst = 0x8A; break;
			case 0x00EF: *dst = 0x8B; break;
			case 0x00EE: *dst = 0x8C; break;
			case 0x00EC: *dst = 0x8D; break;
			case 0x00C4: *dst = 0x8E; break;
			case 0x00C5: *dst = 0x8F; break;
			case 0x00C9: *dst = 0x90; break;
			case 0x00E6: *dst = 0x91; break;
			case 0x00C6: *dst = 0x92; break;
			case 0x00F4: *dst = 0x93; break;
			case 0x00F6: *dst = 0x94; break;
			case 0x00F2: *dst = 0x95; break;
			case 0x00FB: *dst = 0x96; break;
			case 0x00F9: *dst = 0x97; break;
			case 0x00FF: *dst = 0x98; break;
			case 0x00D6: *dst = 0x99; break;
			case 0x00DC: *dst = 0x9A; break;
			case 0x00A2: *dst = 0x9B; break;
			case 0x00A3: *dst = 0x9C; break;
			case 0x00A5: *dst = 0x9D; break;
			case 0x20A7: *dst = 0x9E; break;
			case 0x0192: *dst = 0x9F; break;
			case 0x00E1: *dst = 0xA0; break;
			case 0x00ED: *dst = 0xA1; break;
			case 0x00F3: *dst = 0xA2; break;
			case 0x00FA: *dst = 0xA3; break;
			case 0x00F1: *dst = 0xA4; break;
			case 0x00D1: *dst = 0xA5; break;
			case 0x00AA: *dst = 0xA6; break;
			case 0x00BA: *dst = 0xA7; break;
			case 0x00BF: *dst = 0xA8; break;
			case 0x2310: *dst = 0xA9; break;
			case 0x00AC: *dst = 0xAA; break;
			case 0x00BD: *dst = 0xAB; break;
			case 0x00BC: *dst = 0xAC; break;
			case 0x00A1: *dst = 0xAD; break;
			case 0x00AB: *dst = 0xAE; break;
			case 0x00BB: *dst = 0xAF; break;
			case 0x2591: *dst = 0xB0; break;
			case 0x2592: *dst = 0xB1; break;
			case 0x2593: *dst = 0xB2; break;
			case 0x2502: *dst = 0xB3; break;
			case 0x2524: *dst = 0xB4; break;
			case 0x2561: *dst = 0xB5; break;
			case 0x2562: *dst = 0xB6; break;
			case 0x2556: *dst = 0xB7; break;
			case 0x2555: *dst = 0xB8; break;
			case 0x2563: *dst = 0xB9; break;
			case 0x2551: *dst = 0xBA; break;
			case 0x2557: *dst = 0xBB; break;
			case 0x255D: *dst = 0xBC; break;
			case 0x255C: *dst = 0xBD; break;
			case 0x255B: *dst = 0xBE; break;
			case 0x2510: *dst = 0xBF; break;
			case 0x2514: *dst = 0xC0; break;
			case 0x2534: *dst = 0xC1; break;
			case 0x252C: *dst = 0xC2; break;
			case 0x251C: *dst = 0xC3; break;
			case 0x2500: *dst = 0xC4; break;
			case 0x253C: *dst = 0xC5; break;
			case 0x255E: *dst = 0xC6; break;
			case 0x255F: *dst = 0xC7; break;
			case 0x255A: *dst = 0xC8; break;
			case 0x2554: *dst = 0xC9; break;
			case 0x2569: *dst = 0xCA; break;
			case 0x2566: *dst = 0xCB; break;
			case 0x2560: *dst = 0xCC; break;
			case 0x2550: *dst = 0xCD; break;
			case 0x256C: *dst = 0xCE; break;
			case 0x2567: *dst = 0xCF; break;
			case 0x2568: *dst = 0xD0; break;
			case 0x2564: *dst = 0xD1; break;
			case 0x2565: *dst = 0xD2; break;
			case 0x2559: *dst = 0xD3; break;
			case 0x2558: *dst = 0xD4; break;
			case 0x2552: *dst = 0xD5; break;
			case 0x2553: *dst = 0xD6; break;
			case 0x256B: *dst = 0xD7; break;
			case 0x256A: *dst = 0xD8; break;
			case 0x2518: *dst = 0xD9; break;
			case 0x250C: *dst = 0xDA; break;
			case 0x2588: *dst = 0xDB; break;
			case 0x2584: *dst = 0xDC; break;
			case 0x258C: *dst = 0xDD; break;
			case 0x2590: *dst = 0xDE; break;
			case 0x2580: *dst = 0xDF; break;
			case 0x03B1: *dst = 0xE0; break;
			case 0x00DF: *dst = 0xE1; break;
			case 0x0393: *dst = 0xE2; break;
			case 0x03C0: *dst = 0xE3; break;
			case 0x03A3: *dst = 0xE4; break;
			case 0x03C3: *dst = 0xE5; break;
			case 0x00B5: *dst = 0xE6; break;
			case 0x03C4: *dst = 0xE7; break;
			case 0x03A6: *dst = 0xE8; break;
			case 0x0398: *dst = 0xE9; break;
			case 0x03A9: *dst = 0xEA; break;
			case 0x03B4: *dst = 0xEB; break;
			case 0x221E: *dst = 0xEC; break;
			case 0x03C6: *dst = 0xED; break;
			case 0x03B5: *dst = 0xEE; break;
			case 0x2229: *dst = 0xEF; break;
			case 0x2261: *dst = 0xF0; break;
			case 0x00B1: *dst = 0xF1; break;
			case 0x2265: *dst = 0xF2; break;
			case 0x2264: *dst = 0xF3; break;
			case 0x2320: *dst = 0xF4; break;
			case 0x2321: *dst = 0xF5; break;
			case 0x00F7: *dst = 0xF6; break;
			case 0x2248: *dst = 0xF7; break;
			case 0x00B0: *dst = 0xF8; break;
			case 0x2219: *dst = 0xF9; break;
			case 0x00B7: *dst = 0xFA; break;
			case 0x221A: *dst = 0xFB; break;
			case 0x207F: *dst = 0xFC; break;
			case 0x00B2: *dst = 0xFD; break;
			case 0x25A0: *dst = 0xFE; break;
			case 0x00A0: *dst = 0xFF; break;
			default: *dst = 0xFE;
		}
		dst++;
		src = nextUTF8(src);
	}
	*dst = 0;
	return count;
}
