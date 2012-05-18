/*  Singleplayer game code.
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#include "common.h"
#include "sound.h"

#ifndef WIN32
#include <unistd.h>
#include <fcntl.h>
#else
#include <windows.h>
#endif

static struct player_state p;
unsigned char level = 0;
unsigned char saved_level = 0;
static int bonus;

enum game_state
{
  GS_NEUTRAL,
  GS_SHINE_GET,
  GS_PINK
};

static SDL_Surface* spbg;

static enum game_state state;
static Uint32 first_tick;
static Uint32 now;
static Uint32 last_avalanche;
static int next_evil;

enum game_mode
{
  GM_TIME_ATTACK, /* Clear level in given timeframe */
  GM_AVALANCHE,   /* Keep field from filling up */
  GM_GRAVITY,     /* Balls are affected by gravity */
  GM_INV_GRAVITY, /* Balls are affected by gravity, upwards */
};

#include "singleplayer_levels.c"

static int random_bubble(struct player_state* p)
{
  int i, j, result;
  int tried = 0;

  if(level < sizeof(levels) / sizeof(levels[0]) && levels[level].has_joker)
  {
    if((rng() % 10) == 0)
      return 8;
  }

  while(tried != 0xFF)
  {
    result = rng() % 8;

    tried |= (1 << result);

    for(i = 0; i < field_height; ++i)
    {
      for(j = 0; j < WIDTH(i); ++j)
      {
        if(p->field[i][j] == result + 1)
          return result;
      }
    }
  }

  return rng() % 8;
}

static void init_field()
{
  int i, j, k, color;

  if(level < sizeof(levels) / sizeof(levels[0]))
  {
    for(i = 0, k = 0; i < field_height; ++i)
    {
      for(j = 0; j < WIDTH(i); ++j, ++k)
      {
        p.field[i][j] = levels[level].colors[k];
      }
    }

    rng_seed = levels[level].seed;
    bonus = 0;
  }
  else
  {
    for(i = 0; i < field_height / 2; ++i)
    {
      for(j = 0; j < WIDTH(i); ++j)
      {
        color = (rng() % 8) + 1;

        p.field[i][j] = color;
      }
    }
  }

  for(; i < field_height; ++i)
  {
    for(j = 0; j < WIDTH(i); ++j)
    {
      p.field[i][j] = 0;
    }
  }

  p.bubble = random_bubble(&p);
  p.next_bubble = random_bubble(&p);

  init_player(&p);

  state = GS_NEUTRAL;
}

static void game_tick(int paint)
{
  int i, j, k;
  int yoff, xoff;
  int bx, by;
  SDL_Rect rect;
  SDL_Rect rect2;

  xoff = 30, yoff = 40;

  p.angle += p.right * 90 * time_step;

  if(p.angle < -85)
    p.angle = -85;
  else if(p.angle > 85)
    p.angle = 85;

  if(level == 255)
  {
    k = 0;

    for(i = 0; i < field_height; ++i)
    {
      for(j = 0; j < WIDTH(i); ++j)
      {
        if(p.field[i][j])
          ++k;
      }
    }

    for(i = 0; i < sizeof(p.mbubbles) / sizeof(p.mbubbles[0]); ++i)
    {
      if(p.mbubbles[i].color && !p.mbubbles[i].falling)
        ++k;
    }

    k += p.evil_bubble_count;

    if(k < 20)
    {
      for(i = 0; i < 5; ++i)
        p.evil_bubbles[p.evil_bubble_count++] = (rand() % 8) + 1;
    }
  }

  for(k = 0; k < sizeof(p.mbubbles) / sizeof(p.mbubbles[0]); ++k)
  {
    struct moving_bubble* b = &p.mbubbles[k];

    if(!b->color)
      continue;

    if(b->y > 480)
      b->color = 0;

    if(!b->falling)
    {
      b->x += b->velx * time_step;
      b->y += b->vely * time_step;

      if(level < sizeof(levels) / sizeof(levels[0]))
      {
        if(levels[level].mode == GM_GRAVITY)
          b->vely += 1;
        else if(levels[level].mode == GM_INV_GRAVITY)
          b->vely -= 3;
      }

      if(b->x < 0)
      {
        b->x = -b->x;
        b->velx = -b->velx;

        if(sound_enable)
          sounds[2].pos = 0;
      }
      else if(b->x > max_x)
      {
        b->x = 2 * max_x - b->x;
        b->velx = -b->velx;

        if(sound_enable)
          sounds[2].pos = 0;
      }

      for(i = -1; i < field_height; ++i)
      {
        for(j = 0; j < WIDTH(i); ++j)
        {
          float posx, posy;
          float dirx, diry;

          if(i != -1 && !p.field[i][j])
            continue;

          posx = j * 32.0f + ((i & 1) ? 32.0f : 16.0f);
          posy = i * 28.0f + 16.0f;

          dirx = posx - (b->x + 16.0f);
          diry = posy - (b->y + 16.0f);

          float minrage = 784.0f;

          if(level < sizeof(levels) / sizeof(levels[0]) && levels[level].mode == GM_GRAVITY)
            minrage = 32 * 32;

          if(dirx * dirx + diry * diry < minrage)
          {
            if(fabs(dirx) > fabs(diry))
            {
              by = i;

              if(dirx > 0)
                bx = j - 1;
              else
                bx = j + 1;
            }
            else // fabs(dirx) <= fabs(diry)
            {
              if(diry > 0)
                by = i - 1;
              else
                by = i + 1;

              if(by & 1)
              {
                if(dirx > 0)
                  bx = j - 1;
                else
                  bx = j;
              }
              else
              {
                if(dirx > 0)
                  bx = j;
                else
                  bx = j + 1;
              }
            }

            if(by > 11)
            {
              int a, b;

              for(a = 0; a < field_height; ++a)
                for(b = 0; b < WIDTH(a); ++b)
                  remove_bubble(&p, b, a, 0);

              state = GS_PINK;

              for(a = 0; a < sizeof(p.mbubbles) / sizeof(p.mbubbles[0]); ++a)
                p.mbubbles[a].falling = 1;
            }
            else
            {
              if(stick(&p, bx, by, b->color))
              {
                int a;

                if(level != 255)
                  state = GS_SHINE_GET;

                for(a = 0; a < sizeof(p.mbubbles) / sizeof(p.mbubbles[0]); ++a)
                  p.mbubbles[a].falling = 1;
              }

              b->color = 0;
            }

            goto collide;
          }
        }
      }

collide:;
    }
    else
    {
      b->vely += 1500.0f * time_step;
      b->x += b->velx * time_step;
      b->y += b->vely * time_step;
    }
  }

  if(paint)
  {
    int draw_anyway = 0;

    if(p.bubble > 7 || p.next_bubble > 7)
      draw_anyway = 1;

    if(p.dirty_minx < p.dirty_maxx)
    {
      SET_RECT(rect, xoff + p.dirty_minx, yoff + p.dirty_miny,
               p.dirty_maxx - p.dirty_minx,
               p.dirty_maxy - p.dirty_miny);
      SDL_BlitSurface(spbg, &rect, screen, &rect);
    }

    for(i = 0; i < sizeof(p.mbubbles) / sizeof(p.mbubbles[0]); ++i)
    {
      if(p.mbubbles[i].lastpaintx == INT_MIN)
        continue;

      SET_RECT(rect, p.mbubbles[i].lastpaintx + xoff, p.mbubbles[i].lastpainty + yoff, 32, 32);

      SDL_BlitSurface(spbg, &rect, screen, &rect);

      mark_dirty(&p, p.mbubbles[i].lastpaintx, p.mbubbles[i].lastpainty, 32, 32);

      p.mbubbles[i].lastpaintx = INT_MIN;
    }

    if(p.dirty_minx < p.dirty_maxx || (level < sizeof(levels) / sizeof(levels[0]) && levels[level].has_joker))
    {
      for(i = 0; i < 10; ++i)
      {
        for(j = 0; j < WIDTH(i); ++j)
        {
          if(!p.field[i][j])
            continue;

          rect.x = xoff + j * 32 + ((i & 1) ? 16 : 0);
          rect.y = yoff + i * 28;
          rect.w = 32;
          rect.h = 32;

          int color = p.field[i][j] - 1;

          if(color < 8)
            cond_blit(&p, bubbles[color], 0, screen, &rect);
          else
          {
            mark_dirty(&p, rect.x - xoff, rect.y - yoff, rect.w, rect.h);
            cond_blit(&p, bubbles[(now / 100) % 8], 0, screen, &rect);
          }
        }
      }
    }

    if(p.last_angle != p.angle
    || (   78 <= p.dirty_maxx && 178 >= p.dirty_minx
        && 316 <= p.dirty_maxy && 432 >= p.dirty_miny))
    {
      mark_dirty(&p, 78, 316, 100, 100);

      p.last_angle = p.angle;
    }

    if(p.dirty_minx < p.dirty_maxx || draw_anyway)
    {
      rect.x = xoff + 78;
      rect.y = yoff + 316;
      rect.w = 100;
      rect.h = 100;

      if(draw_anyway)
        mark_dirty(&p, rect.x - xoff, rect.y - yoff, 100, 100);

      cond_blit(&p, spbg, &rect, screen, &rect);

      for(i = 10; i < field_height; ++i)
      {
        for(j = 0; j < WIDTH(i); ++j)
        {
          if(!p.field[i][j])
            continue;

          rect2.x = xoff + j * 32 + ((i & 1) ? 16 : 0);
          rect2.y = yoff + i * 28;
          rect2.w = 32;
          rect2.h = 32;

          int color = p.field[i][j] - 1;

          if(color < 8)
            cond_blit(&p, bubbles[color], 0, screen, &rect2);
          else
            cond_blit(&p, bubbles[(now / 100) % 8], 0, screen, &rect2);
        }
      }

      if((78 <= p.dirty_maxx && 178 >= p.dirty_minx
       && 316 <= p.dirty_maxy && 432 >= p.dirty_miny) || draw_anyway)
      {
        rect2.x = xoff + 112;
        rect2.y = yoff + 350;
        rect2.w = 32;
        rect2.h = 32;

        if(p.bubble < 8)
          SDL_BlitSurface(bubbles[p.bubble], 0, screen, &rect2);
        else
          SDL_BlitSurface(bubbles[(now / 100) % 8], 0, screen, &rect2);
        SDL_BlitSurface(base[(int) ((p.angle + 90.0f) * 128 / 180.0f)], 0, screen, &rect);

        rect2.x = xoff + 112;
        rect2.y = yoff + 400;

        if(p.next_bubble < 8)
          SDL_BlitSurface(bubbles[p.next_bubble], 0, screen, &rect2);
        else
          SDL_BlitSurface(bubbles[(now / 100) % 8], 0, screen, &rect2);
      }
    }

    p.dirty_minx = max_field_width * 32;
    p.dirty_miny = 440;
    p.dirty_maxx = 0;
    p.dirty_maxy = 0;

    for(i = 0; i < sizeof(p.mbubbles) / sizeof(p.mbubbles[0]); ++i)
    {
      if(!p.mbubbles[i].color)
        continue;

      SET_RECT(rect,
               (int) p.mbubbles[i].x + xoff,
               (int) p.mbubbles[i].y + yoff,
               32, 32);

      int color = p.mbubbles[i].color - 1;

      if(color < 8)
        SDL_BlitSurface(bubbles[color], 0, screen, &rect);
      else
      {
        mark_dirty(&p, rect.x - xoff, rect.y - yoff, rect.w, rect.h);
        SDL_BlitSurface(bubbles[(now / 100) % 8], 0, screen, &rect);
      }

      p.mbubbles[i].lastpaintx = rect.x - xoff;
      p.mbubbles[i].lastpainty = rect.y - yoff;
    }

    if(level < sizeof(levels) / sizeof(levels[0]) && levels[level].mode == GM_TIME_ATTACK)
    {
      wchar_t buf[256];

      int remaining = levels[level].time - (now - first_tick) / 1000 + bonus;

      if(remaining <= 0)
      {
        if(levels[level].shoot_bonus)
        {
          bonus += levels[level].shoot_bonus;

          shoot(&p, random_bubble(&p), -1);
          remaining = bonus;
        }
        else
        {
          state = GS_PINK;
          remaining = 0;
        }
      }

#ifndef WIN32
      swprintf(buf, sizeof(buf), L"%u", remaining);
#else
      swprintf(buf, L"%u", remaining);
#endif

      SET_RECT(rect, 355, 104, 256, 128);

      SDL_BlitSurface(spbg, &rect, screen, &rect);

      print_string(0, 483, 144, L"Time remaining", 1);
      print_string(0, 483, 184, buf, 1);
    }
    else if(level < sizeof(levels) / sizeof(levels[0]) && levels[level].mode == GM_AVALANCHE)
    {
      if(now - last_avalanche > levels[level].time * 1000)
      {
        p.evil_bubbles[p.evil_bubble_count++] = random_bubble(&p) + 1;

        last_avalanche = now;
      }
    }

    {
      wchar_t buf[256];

      SET_RECT(rect, 355, 232, 256, 80);

      SDL_BlitSurface(spbg, &rect, screen, &rect);

      if(level != 255)
      {
#ifndef WIN32
        swprintf(buf, sizeof(buf), L"Level %u", level + 1);
#else
        swprintf(buf, L"Level %u", level + 1);
#endif
        print_string(0, 483, 264, buf, 1);
      }
      else
      {
        print_string(0, 483, 264, L"Infinity", 1);
      }
    }
  }
}

void play_single_player()
{
  time_stepms = 4;
  time_step = 0.004f;
  spbg = get_image("spbg.png");

  for(;;)
  {
    Uint32 last_tick = SDL_GetTicks();
    first_tick = last_tick;
    last_avalanche = last_tick;

    init_field();

    SDL_BlitSurface(spbg, 0, screen, 0);

    while(state == GS_NEUTRAL)
    {
      SDL_Event event;

      now = SDL_GetTicks();

      while(now >= last_tick + time_stepms)
      {
        /* Skip delays larger than 5s */
        if(now - last_tick > 5000)
          last_tick = now - 5000;

        last_tick += time_stepms;

        game_tick(now - last_tick < time_stepms);
      }

      while(SDL_PollEvent(&event))
      {
        switch(event.type)
        {
        case SDL_QUIT:

          exit(EXIT_SUCCESS);

          break;

        case SDL_JOYBUTTONDOWN:
          if (event.jbutton.button  == 15 || event.jbutton.button == 0)/* Code same as SDLK_SPACE */
             {
               if(state == GS_NEUTRAL)
                 {
                   if(level < sizeof(levels) / sizeof(levels[0]))
                     {
                       bonus += levels[level].shoot_bonus;
                       shoot(&p, random_bubble(&p), (levels[level].mode == GM_INV_GRAVITY) ? 400 : -1);
                     }
                   else
                     shoot(&p, random_bubble(&p), -1);
                 }            
             }
          if (event.jbutton.button  == 13 || event.jbutton.button == 2)/* Code same as SDLK_ESCAPE */
             {
               if(level == 255)
                 level = saved_level;

#if LINUX || DARWIN
               if(getenv("HOME"))
                 {
                   char confpath[4096];
                   strcpy(confpath, getenv("HOME"));
                   strcat(confpath, "/.pengupoprc");
                   int fd = open(confpath, O_WRONLY | O_CREAT, 0600);

                   if(fd != -1)
                     {
                       lseek(fd, 32, SEEK_SET);
                       write(fd, &level, 1);
                       close(fd);
                     }
                 }
#elif defined(WIN32)
                     {
                   HKEY k_config;

                   if(ERROR_SUCCESS == RegCreateKey(HKEY_CURRENT_USER, "Software\\Junoplay.com\\Pengupop\\Config", &k_config))
                     {
                       char str[64];
                       snprintf(str, sizeof(str), "%d", (level ^ 0x7236143));
                       str[63] = 0;
                       RegSetValueEx(k_config, "bananas", 0, REG_SZ, str, strlen(str));
                     }
                     }
#endif
               return;
             }
           break;

        case SDL_JOYAXISMOTION:

          if ((event.jaxis.value > -3200) || (event.jaxis.value < 3200))  
             {
              switch (event.jaxis.axis)
                 {
                  case 0:
                   if (p.right == -1)/* Code same as SDL_KEYUP:SDLK_LEFT */
                      p.right = 0;
                   if (p.right == 1)/* Code same as SDL_KEYUP:SDLK_RIGHT */
                      p.right = 0;
                    break;
                 }
             }

          if ((event.jaxis.value < -3200) || (event.jaxis.value > 3200))  
             {
              switch (event.jaxis.axis)
                 {
                  case 0:
                   if (event.jaxis.value < -22000)/* Code same as SDL_KEYDOWN:SDLK_LEFT */
                      p.right = -1;
                   if (event.jaxis.value > 22000)/* Code same as SDL_KEYDOWN:SDLK_RIGHT */
                      p.right = 1;
                    break;
                 }
             }
          break;

        case SDL_KEYDOWN:

          switch(event.key.keysym.sym)
          { 
          case SDLK_q:
          case SDLK_ESCAPE:

            if(level == 255)
              level = saved_level;

#if LINUX || DARWIN
            if(getenv("HOME"))
            {
              char confpath[4096];

              strcpy(confpath, getenv("HOME"));
              strcat(confpath, "/.pengupoprc");

              int fd = open(confpath, O_WRONLY | O_CREAT, 0600);

              if(fd != -1)
              {
                lseek(fd, 32, SEEK_SET);
                write(fd, &level, 1);

                close(fd);
              }
            }
#elif defined(WIN32)
            {
              HKEY k_config;

              if(ERROR_SUCCESS == RegCreateKey(HKEY_CURRENT_USER, "Software\\Junoplay.com\\Pengupop\\Config", &k_config))
              {
                char str[64];
                snprintf(str, sizeof(str), "%d", (level ^ 0x7236143));
                str[63] = 0;

                RegSetValueEx(k_config, "bananas", 0, REG_SZ, str, strlen(str));
              }
            }
#endif

            return;

          case SDLK_x:
          case SDLK_RETURN:
          case SDLK_SPACE:
          case SDLK_UP:

            if(state == GS_NEUTRAL)
            {
              if(level < sizeof(levels) / sizeof(levels[0]))
              {
                bonus += levels[level].shoot_bonus;

                shoot(&p, random_bubble(&p), (levels[level].mode == GM_INV_GRAVITY) ? 400 : -1);
              }
              else
                shoot(&p, random_bubble(&p), -1);
            }

            break;

          case SDLK_LEFT:

            p.right = -1;

            break;

          case SDLK_RIGHT:

            p.right = 1;

            break;

          case 's':

            sound_enable = !sound_enable;

            break;

          case 'f':

#ifndef WIN32
            SDL_WM_ToggleFullScreen(screen);
#else
            if(fullscreen)
              screen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE);
            else
              screen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE | SDL_FULLSCREEN);

            SDL_BlitSurface(spbg, 0, screen, 0);

            p.dirty_minx = 0;
            p.dirty_miny = 0;
            p.dirty_maxx = max_field_width * 32;
            p.dirty_maxy = 440;
#endif
            fullscreen = !fullscreen;

            break;

          default:;
          }

          break;

        case SDL_KEYUP:

          switch(event.key.keysym.sym)
          {
          case SDLK_LEFT:

            if(p.right == -1)
              p.right = 0;

            break;

          case SDLK_RIGHT:

            if(p.right == 1)
              p.right = 0;

            break;

          default:;
          }

          break;
        }
      }

      if(p.evil_bubble_count && !next_evil)
      {
        int i = 0;

        while(p.evil_bubble_count && i < sizeof(p.mbubbles) / sizeof(p.mbubbles[0]))
        {
          if(p.mbubbles[i].color != 0 || p.mbubbles[i].lastpaintx != INT_MIN)
          {
            ++i;

            continue;
          }

          float rand = sin(pow(p.evil_bubble_seed++, 4.5)) * 0.5 + 0.5;
          float angle = rand * 60 - 30;

          p.mbubbles[i].falling = 0;
          p.mbubbles[i].velx = sin(angle / 180 * M_PI) * bubble_speed;
          p.mbubbles[i].vely = -cos(angle / 180 * M_PI) * bubble_speed;
          p.mbubbles[i].x = 112.0f;
          p.mbubbles[i].y = 400.0f;
          p.mbubbles[i].color = p.evil_bubbles[0];

          --p.evil_bubble_count;

          memmove(p.evil_bubbles, &p.evil_bubbles[1], p.evil_bubble_count);

          next_evil = 10;

          break;
        }
      }

      if(next_evil)
        --next_evil;

      SDL_UpdateRect(screen, 0, 0, 0, 0);
    }

    Uint32 message_until = now + 2000;
    p.right = 0;

    for(;;)
    {
      now = SDL_GetTicks();

      SDL_Event event;

      while(SDL_PollEvent(&event))
      {
        if(event.type == SDL_KEYDOWN)
        {
          if(event.key.keysym.sym == SDLK_ESCAPE
          || event.key.keysym.sym == SDLK_SPACE
          || event.key.keysym.sym == SDLK_RETURN)
            message_until = now;
        }
      }

      while(now >= last_tick + time_stepms)
      {
        /* Skip delays larger than 5s */
        if(now - last_tick > 5000)
          last_tick = now - 5000;

        last_tick += time_stepms;

        game_tick(now - last_tick < time_stepms);
      }

      if(now > message_until)
        break;

      SDL_UpdateRect(screen, 0, 0, 0, 0);
    }

    if(state == GS_SHINE_GET)
      ++level;

    if(level == sizeof(levels) / sizeof(levels[0]))
    {
      level = 0;

      for(;;)
      {
        SDL_Event event;

        while(SDL_PollEvent(&event))
        {
          if(event.type == SDL_KEYDOWN)
          {
            if(event.key.keysym.sym == SDLK_ESCAPE
            || event.key.keysym.sym == SDLK_SPACE
            || event.key.keysym.sym == SDLK_RETURN)
              return;
          }
        }

        SDL_BlitSurface(logo, 0, screen, 0);

        print_string(0, 320, 250, L"Congratulations!", 1);
        print_string(0, 320, 290, L"That was all", 1);

        SDL_UpdateRect(screen, 0, 0, 0, 0);
      }

      return;
    }
  }
}
