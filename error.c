/*  Error handling routines.
    Copyright (C) 2006  Morten Hustveit <morten@rashbox.org>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#if WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "error.h"

void info(const char* format, ...)
{
  va_list args;

  va_start(args, format);

  vfprintf(stderr, format, args);
  fwrite("\n", 1, 1, stderr);
}

void fatal_error(const char* format, ...)
{
  va_list args;

  va_start(args, format);

#if WIN32
  char buf[512];

  vsnprintf(buf, sizeof(buf), format, args);
  buf[sizeof(buf) - 1] = 0;

  MessageBox(0, buf, "Fatal Error", MB_OK | MB_ICONEXCLAMATION);
#else
  vfprintf(stderr, format, args);
  fwrite("\n", 1, 1, stderr);
#endif

  exit(EXIT_FAILURE);
}
