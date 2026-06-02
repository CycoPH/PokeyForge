//	Altirra - Atari 800/800XL/5200 emulator
//	Core library - generic bus signal implementation
//	Copyright (C) 2023 Avery Lee
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.
//
//	As a special exception, this library can also be redistributed and/or
//	modified under an alternate license. See COPYING.RMT in the same source
//	archive for details.

#ifndef f_AT_ATCORE_ATASCII_H
#define f_AT_ATCORE_ATASCII_H

#include <vd2/system/vdtypes.h>
#include <vd2/system/constexpr.h>

struct ATATASCIITables {
	uint16 mATASCIIToUnicode[2][128];
	VDCxHashMap<uint16, uint8, 167, 256> mUnicodeToATASCII;
};

extern const ATATASCIITables kATATASCIITables;

// XOR value for indexing by bits 5..6
static inline constexpr uint8 kATInternalToATASCIITable[4] {
	0x20, 0x60, 0x40, 0x00
};

inline constexpr uint8 ATConvertInternalToATASCII(uint8 c) {
	return c ^ kATInternalToATASCIITable[(c >> 5) & 3];
}

#endif
