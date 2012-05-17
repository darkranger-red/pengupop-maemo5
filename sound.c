/**
 * Very simple sample loader and mixer.
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

#include "destroy_group.h"
#include "launch.h"
#include "rebound.h"
#include "stick.h"

#include "sound.h"

SDL_AudioSpec sdl_audio;
struct sound sounds[4];

void load_sounds()
{
  int i;

  SDL_LoadWAV_RW(SDL_RWFromMem(destroy_group, size_destroy_group), 1,
                 &sounds[0].spec, &sounds[0].buf, &sounds[0].len);

  SDL_LoadWAV_RW(SDL_RWFromMem(launch, size_launch), 1,
                 &sounds[1].spec, &sounds[1].buf, &sounds[1].len);

  SDL_LoadWAV_RW(SDL_RWFromMem(rebound, size_rebound), 1,
                 &sounds[2].spec, &sounds[2].buf, &sounds[2].len);

  SDL_LoadWAV_RW(SDL_RWFromMem(stick, size_stick), 1,
                 &sounds[3].spec, &sounds[3].buf, &sounds[3].len);

  for(i = 0; i < 4; ++i)
  {
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    swab(sounds[i].buf, sounds[i].buf, sounds[i].len);
#endif
    sounds[i].pos = sounds[i].len;
  }
}

void SDLCALL sound_callback(void* userdata, Uint8* stream, int len)
{
  int i, j;
  int val;
  signed short* sample = (signed short*) stream;

  len /= 2;

  for(i = 0; i < len; ++i, ++sample)
  {
    val = 0;

    for(j = 0; j < 4; ++j)
    {
      if(sounds[j].pos < sounds[j].len)
      {
        val += *((signed short*) &sounds[j].buf[sounds[j].pos]);
        sounds[j].pos += 2;
      }
    }

    if(val < -32768)
      *sample = -32768;
    else if(val > 32767)
      *sample = 32767;
    else
      *sample = val;
  }
}

