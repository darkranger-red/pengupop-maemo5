/**
 * Font rendering routines.
 *
 * Copyright (C) 2006  Morten Hustveit
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <SDL/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#ifndef WIN32
#include <arpa/inet.h>
#else
#include <winsock.h>
#endif

#include "font_16.h"
#include "font_32.h"
#include "error.h"

SDL_Surface* get_image(const char* name);

struct glyph_info
{
  int character;
  int size;
  int bitmapidx;
  int top;
  int xskip;
  int image_width;
  int image_height;
  float s1;
  float t1;
  float s2;
  float t2;
};

static struct glyph_info* glyphs[2];
static int glyph_counts[2];
static SDL_Surface* fontimages[2];

extern SDL_Surface* screen;

void load_font()
{
  int i;

  glyphs[0] = (struct glyph_info*) font_32;

  glyph_counts[0] = size_font_32 / sizeof(struct glyph_info);

  for(i = 0; i < glyph_counts[0] * sizeof(struct glyph_info) / sizeof(int); ++i)
    ((int*) glyphs[0])[i] = ntohl(((int*) glyphs[0])[i]);

  fontimages[0] = get_image("font_32.png");

  glyphs[1] = (struct glyph_info*) font_16;

  glyph_counts[1] = size_font_16 / sizeof(struct glyph_info);

  for(i = 0; i < glyph_counts[1] * sizeof(struct glyph_info) / sizeof(int); ++i)
    ((int*) glyphs[1])[i] = ntohl(((int*) glyphs[1])[i]);

  fontimages[1] = get_image("font_16.png");

  for(i = 0; i < glyph_counts[1]; ++i)
  {
    if(glyphs[1][i].character == 'j')
      glyphs[1][i].xskip += 1;
  }
}

int has_char(int font, int ch)
{
  int i;

  for(i = 0; i < glyph_counts[font]; ++i)
  {
    if(glyphs[font][i].character == ch)
      return 1;
  }

  return 0;
}

int string_width(int font, const wchar_t* string, size_t length)
{
  const wchar_t* c;
  int i;
  int result = 0;

  for(c = string; length; ++c, --length)
  {
    for(i = 0; i < glyph_counts[font]; ++i)
    {
      if(glyphs[font][i].character == *c)
      {
        result += glyphs[font][i].xskip;

        break;
      }
    }
  }

  return result;
}

void print_string(int font, int x, int y, const wchar_t* string, int align)
{
  SDL_Rect src, dest;
  const wchar_t* c;
  int i;
  int total_width = 0;

  if(align)
  {
    for(c = string; *c; ++c)
    {
      for(i = 0; i < glyph_counts[font]; ++i)
      {
        if(glyphs[font][i].character == *c)
        {
          total_width += glyphs[font][i].xskip;

          break;
        }
      }
    }

    dest.x = x - align * total_width / 2;
  }
  else
    dest.x = x;

  for(c = string; *c; ++c)
  {
    for(i = 0; i < glyph_counts[font]; ++i)
    {
      if(glyphs[font][i].character == *c)
        break;
    }

    if(i == glyph_counts[font])
      continue;

    dest.y = y - glyphs[font][i].top;
    dest.w = glyphs[font][i].image_width;
    dest.h = glyphs[font][i].image_height;

    if(font == 0)
    {
      src.x = (int) (glyphs[font][i].s1 * 512.0f + 0.5f);
      src.y = (int) (glyphs[font][i].t1 * 256.0f + 0.5f);
    }
    else
    {
      src.x = (int) (glyphs[font][i].s1 * 256.0f + 0.5f);
      src.y = (int) (glyphs[font][i].t1 * 128.0f + 0.5f);
    }
    src.w = dest.w;
    src.h = dest.h;

    SDL_BlitSurface(fontimages[font], &src, screen, &dest);

    dest.x += glyphs[font][i].xskip;
  }
}
