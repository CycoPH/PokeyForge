//	Altirra - Atari 800/800XL/5200 emulator
//	Application base library - process exception filter
//	Copyright (C) 2009-2026 Avery Lee
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

#ifndef f_AT_ATAPPBASE_EXCEPTIONFILTER_H
#define f_AT_ATAPPBASE_EXCEPTIONFILTER_H

#include <vd2/system/vdtypes.h>

#ifdef VD_PLATFORM_WINDOWS

#include <windows.h>

// GCC for Win32 doesn't support __try/__except, so we have to work around
// it. This makes some types of exceptions uncatchable, but this is GCC's
// problem.
#if !defined(VD_COMPILER_GCC)

#define AT_EXCEPTIONFILTER_TRY		__try
#define AT_EXCEPTIONFILTER_EXCEPT	__except(ATExceptionFilter(GetExceptionCode(), GetExceptionInformation()))

#else

#define AT_EXCEPTIONFILTER_TRY		if constexpr (true)
#define AT_EXCEPTIONFILTER_EXCEPT	else

#endif

void ATSetExceptionPreFilter(void (*fn)(DWORD code, const EXCEPTION_POINTERS *));

int ATExceptionFilter(DWORD code, EXCEPTION_POINTERS *exp);
LONG WINAPI ATUnhandledExceptionFilter(EXCEPTION_POINTERS *exp);

void ATExceptionFilterSetFullHeapDump(bool enabled);
#endif

#endif
