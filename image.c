/*  Image loading routines.
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

#include <SDL/SDL.h>
#include <zlib.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "error.h"
#include "images.h"

static struct
{
  char name[64];
  uint32_t width;
  uint32_t height;
} infos[4096];

static SDL_Surface* surfaces[4096];
static uint32_t image_count = 0;

static void rotate(unsigned char* dest, unsigned char* src, int width, int height, double angle)
{
  int x, y;
  int sx, sy, stepx, stepy;
  double ca, sa;

  ca = cos(angle);
  sa = sin(angle);

  for(y = 0; y < height; ++y)
  {
    sx = (ca * -50 + sa * (y - 50) + 50) * 65536;
    sy = (ca * (y - 50) - sa * -50 + 50) * 65536;
    stepx = ca * 65536;
    stepy = -sa * 65536;

    for(x = 0; x < width; ++x)
    {
      sx += stepx;
      sy += stepy;

      if(sx < 0 || (sx >> 16) >= width - 1 || sy < 0 || (sy >> 16) >= height - 1)
      {
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
        *dest++ = 0;
      }
      else
      {
        int y0 = (sy >> 16);
        int y1 = y0 + 1;
        int x0 = (sx >> 16);
        int x1 = x0 + 1;

        int w0 = (255 - ((sy & 0xFF00) >> 8)) * (255 - ((sx & 0xFFFF) >> 8)) / 255;
        int w1 = (255 - ((sy & 0xFF00) >> 8)) * ((sx & 0xFFFF) >> 8) / 255;
        int w2 = ((sy & 0xFF00) >> 8) * (255 - ((sx & 0xFFFF) >> 8)) / 255;
        int w3 = 255 - (w0 + w1 + w2);

        int a0 = src[(y0 * width + x0) * 4 + 3] * w0 / 255;
        int a1 = src[(y0 * width + x1) * 4 + 3] * w1 / 255;
        int a2 = src[(y1 * width + x0) * 4 + 3] * w2 / 255;
        int a3 = src[(y1 * width + x1) * 4 + 3] * w3 / 255;

        int r = (src[(y0 * width + x0) * 4] * a0
               + src[(y0 * width + x1) * 4] * a1
               + src[(y1 * width + x0) * 4] * a2
               + src[(y1 * width + x1) * 4] * a3) / 255;
        int g = (src[(y0 * width + x0) * 4 + 1] * a0
               + src[(y0 * width + x1) * 4 + 1] * a1
               + src[(y1 * width + x0) * 4 + 1] * a2
               + src[(y1 * width + x1) * 4 + 1] * a3) / 255;
        int b = (src[(y0 * width + x0) * 4 + 2] * a0
               + src[(y0 * width + x1) * 4 + 2] * a1
               + src[(y1 * width + x0) * 4 + 2] * a2
               + src[(y1 * width + x1) * 4 + 2] * a3) / 255;
        int a = a0 + a1 + a2 + a3;

        dest[0] = r;
        dest[1] = g;
        dest[2] = b;
        dest[3] = a;
        dest += 4;
      }
    }
  }
}

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define swapu32(v)
#else
#define swapu32(v) do { v = (v >> 24) | ((v & 0xFF0000) >> 8) | ((v & 0xFF00) << 8) | (v << 24); } while(0);
#endif

void load_images()
{
  uint32_t i, j, x, y;
  SDL_Surface* s;
  z_stream input;

  memset(&input, 0, sizeof(input));

  input.next_in = (Bytef*) images;
  input.avail_in = size_images;

  if(Z_OK != inflateInit(&input))
    fatal_error("Executable is corrupt");

  input.next_out = (Bytef*) &image_count;
  input.avail_out = 4;

  inflate(&input, Z_SYNC_FLUSH);

  if(!image_count)
  {
    inflateEnd(&input);

    return;
  }

  swapu32(image_count);

  assert(image_count <= sizeof(surfaces) / sizeof(surfaces[0]));

  input.next_out = (Bytef*) infos;
  input.avail_out = image_count * sizeof(infos[0]);
  inflate(&input, Z_SYNC_FLUSH);

  for(i = 0; i < image_count; ++i)
  {
    unsigned char* data;
    unsigned char* dest;

    if(surfaces[i])
      continue;

    swapu32(infos[i].width);
    swapu32(infos[i].height);

    data = malloc(infos[i].width * infos[i].height * 4);

    input.next_out = (Bytef*) data;
    input.avail_out = infos[i].width * infos[i].height * 4;
    inflate(&input, Z_SYNC_FLUSH);

    if(!strcmp(infos[i].name, "base.png"))
    {
      unsigned char* tmp = malloc(infos[i].width * infos[i].height * 4);

      for(j = 0; j < 129; ++j)
      {
        char name[64];
        strcpy(name, infos[i].name);
        strcat(name, "$");
        sprintf(name + strlen(name), "%d", j);

        rotate(tmp, data, infos[i].width, infos[i].height, -M_PI_2 + j / 128.0 * M_PI);

        strcpy(infos[image_count].name, name);
        infos[image_count].width = infos[i].width;
        infos[image_count].height = infos[i].height;

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
        s = SDL_CreateRGBSurface(SDL_SRCALPHA, infos[i].width, infos[i].height, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
#else
        s = SDL_CreateRGBSurface(SDL_SRCALPHA, infos[i].width, infos[i].height, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
#endif

        surfaces[image_count] = s;

        SDL_LockSurface(s);

        for(y = 0; y < s->h; ++y)
        {
          dest = (unsigned char*) s->pixels + y * s->pitch;

          for(x = 0; x < s->w * 4; x += 4)
          {
            dest[x] = tmp[y * s->w * 4 + x];
            dest[x + 1] = tmp[y * s->w * 4 + x + 1];
            dest[x + 2] = tmp[y * s->w * 4 + x + 2];
            dest[x + 3] = tmp[y * s->w * 4 + x + 3];
          }
        }

        SDL_UnlockSurface(s);

        ++image_count;
      }

      free(tmp);
    }

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
    s = SDL_CreateRGBSurface(SDL_SRCALPHA, infos[i].width, infos[i].height, 32, 0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000);
#else
    s = SDL_CreateRGBSurface(SDL_SRCALPHA, infos[i].width, infos[i].height, 32, 0xff000000, 0x00ff0000, 0x0000ff00, 0x000000ff);
#endif

    surfaces[i] = s;

    SDL_LockSurface(s);

    for(y = 0; y < s->h; ++y)
    {
      dest = (unsigned char*) s->pixels + y * s->pitch;

      for(x = 0; x < s->w * 4; x += 4)
      {
        dest[x] = data[y * s->w * 4 + x];
        dest[x + 1] = data[y * s->w * 4 + x + 1];
        dest[x + 2] = data[y * s->w * 4 + x + 2];
        dest[x + 3] = data[y * s->w * 4 + x + 3];
      }
    }

    SDL_UnlockSurface(surfaces[i]);

    free(data);
  }

  inflateEnd(&input);
}

SDL_Surface* get_image(const char* name)
{
  uint32_t i;

  for(i = 0; i < image_count; ++i)
  {
    if(!strcmp(infos[i].name, name))
      return surfaces[i];
  }

  return 0;
}
