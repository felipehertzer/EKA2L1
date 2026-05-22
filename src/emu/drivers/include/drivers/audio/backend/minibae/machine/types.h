/*
 * Copyright (c) 2021 EKA2L1 Team.
 *
 * This file is part of EKA2L1 project.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <common/platform.h>
#include <stdint.h>

#ifndef X_PLATFORM
#if EKA2L1_PLATFORM(WIN32)
#include <commdlg.h>
#include <fcntl.h>
#include <io.h>
#include <mmsystem.h> // for timeGetTime()
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <windowsx.h>

#define X_PLATFORM X_WIN95
#else
#include <pthread.h>
#if EKA2L1_PLATFORM(IOS)
#define X_PLATFORM X_IOS
#elif EKA2L1_PLATFORM(MACOS)
#define X_PLATFORM X_MACINTOSH
#elif EKA2L1_PLATFORM(UNIX)
#define X_PLATFORM X_ANDROID
#endif
#endif
#endif

#ifndef CPU_TYPE
#if EKA2L1_ARCH(ARM64)
#define CPU_TYPE kARM
#elif EKA2L1_ARCH(X64)
#define CPU_TYPE k80X86
#elif EKA2L1_ARCH(ARM)
#define CPU_TYPE kARM
#endif
#endif
