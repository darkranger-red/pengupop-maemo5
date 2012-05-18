/*  Pengupop multiplayer game code and entry point.
    Copyright (C) 2006, 2007, 2008, 2009  Morten Hustveit <morten@rashbox.org>

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

#ifndef WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#else
#include <windows.h>
#include <winsock.h>
#include <io.h>
#include <time.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "error.h"
#include "common.h"
#include "packet.h"
#include "sound.h"

extern unsigned char saved_level;
extern unsigned char level;

extern const char* gameid;

#ifndef WIN32
typedef int SOCKET;
#define INVALID_SOCKET (-1)
const int SOCKET_ERROR = -1;

#define Sleep(n) usleep((n) * 1000)
#define send(a,b,c,d) write((a),(b),(c))
#define recv(a,b,c,d) read((a),(b),(c))

static struct option long_options[] =
{
  { "help", 0, 0, 'h' },
  { "version", 0, 0, 'v' },
  { "no-login", 0, 0, 'n' },
  { "windowed", 0, 0, 'w' },
  { 0, 0, 0, 0 }
};

extern void play_single_player();

#ifndef swprintf
int swprintf(wchar_t *wcs, size_t maxlen, const wchar_t *format, ...);
#endif
#endif

int hwalpha;

int fullscreen = 1; /* XXX: Change to 1 before release */
int sound_enable = 1;
SDL_Surface* screen;

int time_stepms = 8;
float time_step = 0.008f;
unsigned short listenport;
SOCKET listenfd = INVALID_SOCKET;
SOCKET serverfd = INVALID_SOCKET;
SOCKET outpeerfd = INVALID_SOCKET;
SOCKET inpeerfd = INVALID_SOCKET;
int outpeerconnected;
int outpeervalid;
int inpeervalid;
char outbound_handshake[4];
char inbound_handshake[4];
static char inbufferip[sizeof(struct data_packet)];
static char inbufferop[sizeof(struct data_packet)];
static char inbuffer[sizeof(struct data_packet)];
static char outbufferip[sizeof(struct data_packet) * 16];
static char outbufferop[sizeof(struct data_packet) * 16];
static char outbuffer[sizeof(struct data_packet) * 16];
static int inbufferip_state;
static int inbufferop_state;
static int inbuffer_state;
static int outbufferip_state;
static int outbufferop_state;
static int outbuffer_state;

unsigned int tickidx;
static unsigned int last_evil;

Uint32 last_tick = 0;

static int    repeat_sym;
static int    repeat_key;
static Uint32 repeat_time;

struct event eventlog[256];
unsigned int event_count = 0;
int round_logged = 0;

SDL_Surface* splash;
SDL_Surface* chat;
SDL_Surface* logo;
SDL_Surface* background;
SDL_Surface* base[129];
SDL_Surface* bubbles[8];

enum mode
{
  MODE_MODE_SELECT,
  MODE_SINGLEPLAYER_MENU,
  MODE_MAIN_MENU,
  MODE_LOUNGE,
  MODE_WAITING,
  MODE_GAME,
  MODE_SCORE,
  MODE_ABORT_MESSAGE,
  MODE_ABORT_MESSAGE2
};

enum mode mode = MODE_MODE_SELECT;
wchar_t username[17];
wchar_t password[17];
wchar_t message[258];
size_t  message_cursor;  // cursor is before message[message_cursor]
int messageidx;
int won;
int is_server;
int input_locked;
int single_player = 0;
int peer_version = 0;
int auth_level = 0;
int autologin = 0;
int nologin = 0;

#define CHAT_WIDTH 438
#define CHAT_LINES 18

static wchar_t chatlog[CHAT_LINES][269];
static int chatlogpos;

static wchar_t privchatlog[CHAT_LINES][269];
static int privchatlogpos;

enum flag
{
  FLAG_KEY_LEFT = 0x01,
  FLAG_KEY_RIGHT = 0x02,
  FLAG_BUBBLES = 0x04,
  FLAG_VICTORY = 0x08,
  FLAG_LOSS = 0x10,
  FLAG_READY = 0x20
};

static struct player_state players[2];

static void init_field();

unsigned int rng_seed = 0;

unsigned int rng()
{
  rng_seed = (rng_seed + 0xA853753) % 177
           + (rng_seed * 0x8034052);

  return rng_seed;
}

static void pack_float(unsigned char* a, float v)
{
  union
  {
    int ival;
    float fval;
  } u;

  u.fval = v;

  a[0] = u.ival >> 24;
  a[1] = u.ival >> 16;
  a[2] = u.ival >> 8;
  a[3] = u.ival;
}

static float unpack_float(const unsigned char* a)
{
  union
  {
    int ival;
    float fval;
  } u;

  u.ival = (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];

  return u.fval;
}

// Calculate length of current message
// Set cursor at end of msg if too far
size_t message_length()
{
  size_t length = 0;
  while(length < 256 && message[length])
    ++length;
  if (message_cursor > length)
    message_cursor = length;
  return length;
}

// Remove char left of cursor, shift end of message (including trailing \0)
void message_backspace()
{
  size_t length = message_length();
  if (message_cursor > 0)
  {
    memmove(message+message_cursor-1,
            message+message_cursor,
            sizeof(message[0])*(length-message_cursor+1));
    --message_cursor;
  }
}

// Remove char right of cursor, shift end of message (including \0) :
void message_delete()
{
  size_t length = message_length();
  if (message_cursor < length)
  {
    memmove(message+message_cursor,
            message+message_cursor+1,
            sizeof(message[0])*(length-message_cursor));
  }
}


// Insert char at current position : must shift end of msg
void message_insert(wchar_t c)
{
  size_t length = message_length();
  if (length < 256)
  {
    memmove(message+message_cursor+1,
            message+message_cursor,
            sizeof(message[0])*(length-message_cursor+1));
    message[message_cursor] = c;
    ++message_cursor;
  }
}

// Try to move cursor one char left
void cursor_left()
{
  if (message_cursor > 0)
  {
    --message_cursor;
  }
}

// Try to move cursor one char right
void cursor_right()
{
  size_t length = message_length();
  if (message_cursor < length)
  {
    ++message_cursor;
  }
}

void mark_dirty(struct player_state* p, int x, int y, int width, int height)
{
  if(x < p->dirty_minx)
    p->dirty_minx = x;

  if(y < p->dirty_miny)
    p->dirty_miny = y;

  if(x + width > p->dirty_maxx)
    p->dirty_maxx = x + width;

  if(y + height > p->dirty_maxy)
    p->dirty_maxy = y + height;
}

static void scan(struct player_state* p, int x, int y, int color, int* matches, int* match_count)
{
  int i, idx;

  if(x < 0 || x >= WIDTH(y) || y < 0 || y >= field_height)
    return;

  if(!color)
  {
    if(!p->field[y][x])
      return;
  }
  else
  {
    if(p->field[y][x] != 9
    && p->field[y][x] != color)
      return;
  }

  idx = y * max_field_width + x;

  for(i = 0; i < *match_count; ++i)
    if(matches[i] == idx)
      return;

  matches[(*match_count)++] = idx;

  scan(p, x - 1, y, color, matches, match_count);
  scan(p, x + 1, y, color, matches, match_count);

  if(y & 1)
  {
    scan(p, x, y - 1, color, matches, match_count);
    scan(p, x + 1, y - 1, color, matches, match_count);
    scan(p, x, y + 1, color, matches, match_count);
    scan(p, x + 1, y + 1, color, matches, match_count);
  }
  else
  {
    scan(p, x - 1, y - 1, color, matches, match_count);
    scan(p, x, y - 1, color, matches, match_count);
    scan(p, x - 1, y + 1, color, matches, match_count);
    scan(p, x, y + 1, color, matches, match_count);
  }
}

void remove_bubble(struct player_state* p, int x, int y, int evil)
{
  int i;
  struct moving_bubble mbubble;

  mbubble.falling = 1;
  mbubble.evil = evil;
  mbubble.x = x * 32 + ((y & 1) ? 16 : 0);
  mbubble.y = y * 28;
  mbubble.velx = sinf(powf(x, 1.4f) + powf(y, 2.8f)) * 0.5f * 200.0f;
  mbubble.vely = (sinf(powf(x, 3.4f) + powf(y, 0.6f)) - 1.0f) * 25.0f - 300.0f;
  mbubble.color = p->field[y][x];
  mbubble.lastpaintx = mbubble.x;
  mbubble.lastpainty = mbubble.y;

  for(i = 0; i < sizeof(p->mbubbles) / sizeof(p->mbubbles[0]); ++i)
  {
    if(p->mbubbles[i].color == 0 && p->mbubbles[i].lastpaintx == INT_MIN)
    {
      p->mbubbles[i] = mbubble;

      break;
    }
  }

  p->field[y][x] = 0;
}

static void chatlog_append(int is_private, const wchar_t* m)
{
  size_t length = 0;

  while(length < 256 && m[length])
    ++length;

  // Check for call for fight, or message containing username :
  // The username is not searched at the beginning, for we are
  // not interested in detecting our own words.
  if (wcsncmp(m, L"*** You got a call for fight", 28) == 0)
  {
    sounds[0].pos = 0;  // explosion
  }
  if (length > 0 && username[0]!=0 && wcsstr(m+1, username) != NULL)
  {
    sounds[2].pos = 0;  // piou!
  }

  if(string_width(1, m, length) > CHAT_WIDTH)
  {
    wchar_t rbuf[267];
    size_t start, end;
    size_t i;
    wchar_t* buf = &rbuf[10];

    start = 0;

    wcscpy(rbuf, L"          ");

    while(start < length)
    {
      size_t width = 0;
      size_t max_word = start;
      size_t max_char = start;

      for(end = start + 1; end <= length; ++end)
      {
        width = string_width(1, m + start, end - start);

        if(start != 0)
          width += 50;

        if(width >= CHAT_WIDTH)
          break;

        if(isspace(m[end]))
          max_word = end;

        max_char = end;
      }

      if(end > length)
        end = length;
      else if(max_word != start)
        end = max_word;
      else
        end = max_char;

      for(i = 0; i < end - start; ++i)
        buf[i] = m[start + i];
      buf[i] = 0;

      if(start != 0)
        chatlog_append(is_private, buf);
      else
        chatlog_append(is_private, buf);

      start = end;

      while(isspace(m[start]))
        ++start;
    }

    return;
  }

  if(is_private)
  {
    wcsncpy(privchatlog[privchatlogpos % CHAT_LINES], m, 268);

    ++privchatlogpos;
  }
  else
  {
    wcsncpy(chatlog[chatlogpos % CHAT_LINES], m, 268);

    ++chatlogpos;
  }
}

static void submit_events();
static void add_event(unsigned int tick, int data);

static void log_round()
{
  wchar_t buf[256];

  if(round_logged)
    return;

  round_logged = 1;
  message[0] = 0;

  if(won)
    add_event(tickidx, EVENT_WIN);
  else
    add_event(tickidx, EVENT_LOSE);

  submit_events();

#ifndef WIN32
  if(won)
    swprintf(buf, sizeof(buf), L"*** VICTORY! Won %u of %u total.  Press enter to continue.", players[0].score, players[0].score + players[1].score);
  else
    swprintf(buf, sizeof(buf), L"*** DEFEAT! Won %u of %u total.  Press enter to continue.", players[0].score, players[0].score + players[1].score);
#else
  if(won)
    swprintf(buf, L"*** VICTORY! Won %u of %u total.  Press enter to continue.", players[0].score, players[0].score + players[1].score);
  else
    swprintf(buf, L"*** DEFEAT!  Won %u of %u total.  Press enter to continue.", players[0].score, players[0].score + players[1].score);
#endif

  chatlog_append(1, buf);
}

/* XXX: Modifies p->payload_size */
static void send_packet(struct data_packet* p)
{
  int packet_size = 3 + p->payload_size;
  int* state = 0;
  char* buffer;
  SOCKET* fd;

  if(single_player)
    return;

  if(p->packet_type >= 0x10)
  {
    if(inpeerfd != INVALID_SOCKET && inpeervalid)
      state = &outbufferip_state, buffer = outbufferip, fd = &inpeerfd;
    else if(outpeerfd != INVALID_SOCKET && outpeervalid)
      state = &outbufferop_state, buffer = outbufferop, fd = &outpeerfd;
  }

  if(!state)
    state = &outbuffer_state, buffer = outbuffer, fd = &serverfd;

  if(*state + packet_size > sizeof(outbuffer))
  {
    close(*fd);
    *fd = INVALID_SOCKET;

    wcscpy(message, L"Network Output Buffer Full");

    mode = MODE_ABORT_MESSAGE;

    return;
  }

  p->payload_size = htons(p->payload_size);

  memcpy(buffer + *state, p, packet_size);
  *state += packet_size;
}

static void submit_events()
{
  if(!event_count)
    return;

  struct data_packet p;
  p.packet_type = packet_eventlog;
  p.payload_size = sizeof(struct event) * event_count;

  memcpy(p.eventlog.events, eventlog, sizeof(struct event) * event_count);

  send_packet(&p);

  event_count = 0;
}

static void add_event(unsigned int tick, int data)
{
  if(single_player)
    return;

  if(event_count == 256)
    submit_events();

  struct event* e = &eventlog[event_count++];

  e->tick[0] = (tick >> 16) & 0xFF;
  e->tick[1] = (tick >> 8) & 0xFF;
  e->tick[2] = tick & 0xFF;
  e->data = data;
}

static void init_movement_packet(struct data_packet* p)
{
  p->packet_type = packet_movement;
  p->payload_size = 6;

  pack_float(p->movement.angle, players[0].angle);
  p->movement.flags = 0;

  if(players[0].right == 1)
    p->movement.flags |= FLAG_KEY_RIGHT;
  else if(players[0].right == -1)
    p->movement.flags |= FLAG_KEY_LEFT;

  p->movement.bubbles = players[0].bubble | (players[0].next_bubble << 4);
}

static int test_group(struct player_state* p, int bx, int by, int color)
{
  int matches[max_field_width * field_height];
  int match_count = 0;
  int i;

  scan(p, bx, by, color, matches, &match_count);

  if(match_count >= 3)
  {
    if(p != &players[1] && sound_enable)
      sounds[0].pos = 0;

    for(i = 0; i < match_count; ++i)
    {
      remove_bubble(p, matches[i] % max_field_width,
                    matches[i] / max_field_width, 0);
    }

    return 1;
  }

  return 0;
}

int stick(struct player_state* p, int bx, int by, int color)
{
  int matches[max_field_width * field_height];
  int match_count = 0;
  int i, j, k;

  if(by >= field_height)
    return 0;

  if(p != &players[1] && sound_enable)
    sounds[3].pos = 0;

  p->field[by][bx] = color;
  mark_dirty(p, bx * 32 + ((by & 1) ? 16 : 0), by * 28, 32, 32);

  int broken = 0;

  if(color <= 8)
  {
    if(test_group(p, bx, by, color))
      broken = 1;
  }
  else
  {
    for(i = 1; i <= 8; ++i)
    {
      p->field[by][bx] = i;

      if(test_group(p, bx, by, i))
        broken = 1;
    }

    if(broken)
      p->field[by][bx] = 0;
    else
      p->field[by][bx] = color;
  }

  if(broken)
  {
    match_count = 0;

    for(i = 0; i < WIDTH(0); ++i)
      scan(p, i, 0, 0, matches, &match_count);

    if(!match_count)
    {
      if(p == &players[0])
        won = 1;
      else
        won = 0;

      input_locked = 1;

      for(i = 0; i < field_height; ++i)
      {
        for(j = 0; j < WIDTH(i); ++j)
        {
          if(!p->field[i][j])
            continue;

          remove_bubble(p, j, i, 0);
        }
      }

      return 1;
    }

    for(i = 0; i < field_height; ++i)
    {
      for(j = 0; j < WIDTH(i); ++j)
      {
        if(!p->field[i][j])
          continue;

        for(k = 0; k < match_count; ++k)
          if(matches[k] == i * max_field_width + j)
            break;

        if(k == match_count)
        {
          if(peer_version < 3)
          {
            if(p == &players[0])
            {
              k = players[1].evil_bubble_count++;

              players[1].evil_bubbles[k] = p->field[i][j];
            }
            else
            {
              k = players[0].evil_bubble_count++;

              players[0].evil_bubbles[k] = p->field[i][j];
            }
          }
          else
          {
            if(p == &players[0])
            {
              if(players[1].max_evil < 4)
                ++players[1].max_evil;
            }
            else
            {
              if(players[0].max_evil < 4)
                ++players[0].max_evil;
            }
          }

          remove_bubble(p, j, i, 1);
        }
      }
    }
  }

  return 0;
}

void cond_blit(struct player_state* p, SDL_Surface* source, SDL_Rect* source_rect, SDL_Surface* dest, SDL_Rect* dest_rect)
{
  int xoff, yoff;

  if(p != &players[1])
    xoff = 30, yoff = 40;
  else
    xoff = 355, yoff = 40;

  if(p->dirty_minx >= p->dirty_maxx)
    return;

  if(!dest_rect
  || (   dest_rect->x <= p->dirty_maxx + xoff
      && dest_rect->x + dest_rect->w >= p->dirty_minx + xoff
      && dest_rect->y <= p->dirty_maxy + yoff
      && dest_rect->y + dest_rect->h >= p->dirty_miny + yoff))
  {
    SDL_BlitSurface(source, source_rect, dest, dest_rect);
  }
}

static void game_tick(int paint)
{
  int i, j, k;
  int player;
  int yoff, xoff;
  int bx, by;
  SDL_Rect rect;
  SDL_Rect rect2;

  for(player = 0; player < 2; ++player)
  {
    struct player_state* p = &players[player];

    if(single_player && player > 0)
      break;

    if(player == 0)
      xoff = 30, yoff = 40;
    else
      xoff = 355, yoff = 40;

    p->angle += p->right * 90 * time_step;

    if(p->angle < -85)
      p->angle = -85;
    else if(p->angle > 85)
      p->angle = 85;

    for(k = 0; k < sizeof(p->mbubbles) / sizeof(p->mbubbles[0]); ++k)
    {
      struct moving_bubble* b = &p->mbubbles[k];

      if(!b->color)
        continue;

      if(!b->falling)
      {
        b->x += b->velx * time_step;
        b->y += b->vely * time_step;

        if(b->x < 0)
        {
          b->x = -b->x;
          b->velx = -b->velx;

          if(player == 0 && sound_enable)
            sounds[2].pos = 0;
        }
        else if(b->x > max_x)
        {
          b->x = 2 * max_x - b->x;
          b->velx = -b->velx;

          if(player == 0 && sound_enable)
            sounds[2].pos = 0;
        }

        for(i = -1; i < field_height; ++i)
        {
          for(j = 0; j < WIDTH(i); ++j)
          {
            float posx, posy;
            float dirx, diry;

            if(i != -1 && !p->field[i][j])
              continue;

            posx = j * 32.0f + ((i & 1) ? 32.0f : 16.0f);
            posy = i * 28.0f + 16.0f;

            dirx = posx - (b->x + 16.0f);
            diry = posy - (b->y + 16.0f);

            if(dirx * dirx + diry * diry < 784.0f)
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

              if(by > 11 && !input_locked)
              {
                if(single_player)
                {
                  SDL_BlitSurface(background, 0, screen, 0);

                  init_field();

                  last_tick = SDL_GetTicks();
                  game_tick(1);
                }
                else if(is_server)
                {
                  struct data_packet packet;

                  init_movement_packet(&packet);

                  if(p == &players[0])
                  {
                    packet.movement.flags |= FLAG_LOSS;

                    won = 0;
                    ++players[1].score;
                  }
                  else
                  {
                    packet.movement.flags |= FLAG_VICTORY;

                    won = 1;
                    ++players[0].score;
                  }

                  send_packet(&packet);

                  players[0].ready = 0;
                  players[1].ready = 0;

                  log_round();

                  mode = MODE_SCORE;
                }

                input_locked = 1;
              }

              stick(p, bx, by, b->color);

              b->color = 0;

              goto collide;
            }
          }
        }

collide:;

        if(mode == MODE_SCORE)
          return;
      }
      else
      {
        b->vely += 1500.0f * time_step;
        b->x += b->velx * time_step;
        b->y += b->vely * time_step;

        if(b->y > 480)
        {
          if(b->evil && peer_version >= 3)
          {
            if(p == &players[0] && players[1].max_evil)
            {
              k = players[1].evil_bubble_count++;
              --players[1].max_evil;

              players[1].evil_bubbles[k] = b->color;
            }
            else if(p == &players[1] && players[0].max_evil)
            {
              k = players[0].evil_bubble_count++;
              --players[0].max_evil;

              players[0].evil_bubbles[k] = b->color;
            }
          }

          b->color = 0;
        }
      }
    }

    if(paint)
    {
      if(p->dirty_minx < p->dirty_maxx)
      {
        SET_RECT(rect, xoff + p->dirty_minx, yoff + p->dirty_miny,
                 p->dirty_maxx - p->dirty_minx,
                 p->dirty_maxy - p->dirty_miny);
        SDL_BlitSurface(background, &rect, screen, &rect);
      }

      for(i = 0; i < sizeof(p->mbubbles) / sizeof(p->mbubbles[0]); ++i)
      {
        if(p->mbubbles[i].lastpaintx == INT_MIN)
          continue;

        SET_RECT(rect, p->mbubbles[i].lastpaintx + xoff, p->mbubbles[i].lastpainty + yoff, 32, 32);

        SDL_BlitSurface(background, &rect, screen, &rect);

        mark_dirty(p, p->mbubbles[i].lastpaintx, p->mbubbles[i].lastpainty, 32, 32);

        p->mbubbles[i].lastpaintx = INT_MIN;
      }

      if(p->dirty_minx < p->dirty_maxx)
      {
        for(i = 0; i < 10; ++i)
        {
          for(j = 0; j < WIDTH(i); ++j)
          {
            if(!p->field[i][j])
              continue;

            rect.x = xoff + j * 32 + ((i & 1) ? 16 : 0);
            rect.y = yoff + i * 28;
            rect.w = 32;
            rect.h = 32;

            cond_blit(p, bubbles[p->field[i][j] - 1], 0, screen, &rect);
          }
        }
      }

      if(p->last_angle != p->angle
      || (   78 <= p->dirty_maxx && 178 >= p->dirty_minx
          && 316 <= p->dirty_maxy && 432 >= p->dirty_miny))
      {
        mark_dirty(p, 78, 316, 100, 100);

        p->last_angle = p->angle;
      }

      if(p->dirty_minx < p->dirty_maxx)
      {
        rect.x = xoff + 78;
        rect.y = yoff + 316;
        rect.w = 100;
        rect.h = 100;

        cond_blit(p, background, &rect, screen, &rect);

        for(i = 10; i < field_height; ++i)
        {
          for(j = 0; j < WIDTH(i); ++j)
          {
            if(!p->field[i][j])
              continue;

            rect2.x = xoff + j * 32 + ((i & 1) ? 16 : 0);
            rect2.y = yoff + i * 28;
            rect2.w = 32;
            rect2.h = 32;

            cond_blit(p, bubbles[p->field[i][j] - 1], 0, screen, &rect2);
          }
        }

        if(78 <= p->dirty_maxx && 178 >= p->dirty_minx
        && 316 <= p->dirty_maxy && 432 >= p->dirty_miny)
        {
          rect2.x = xoff + 112;
          rect2.y = yoff + 350;
          rect2.w = 32;
          rect2.h = 32;

          SDL_BlitSurface(bubbles[p->bubble], 0, screen, &rect2);
          SDL_BlitSurface(base[(int) ((p->angle + 90.0f) * 128 / 180.0f)], 0, screen, &rect);

          rect2.x = xoff + 112;
          rect2.y = yoff + 400;

          SDL_BlitSurface(bubbles[p->next_bubble], 0, screen, &rect2);
        }
      }

      p->dirty_minx = max_field_width * 32;
      p->dirty_miny = 440;
      p->dirty_maxx = 0;
      p->dirty_maxy = 0;

      for(i = 0; i < sizeof(p->mbubbles) / sizeof(p->mbubbles[0]); ++i)
      {
        if(!p->mbubbles[i].color)
          continue;

        SET_RECT(rect,
                 (int) p->mbubbles[i].x + xoff,
                 (int) p->mbubbles[i].y + yoff,
                 32, 32);

        SDL_BlitSurface(bubbles[p->mbubbles[i].color - 1], 0, screen, &rect);

        p->mbubbles[i].lastpaintx = rect.x - xoff;
        p->mbubbles[i].lastpainty = rect.y - yoff;
      }
    }
  }

  if(single_player && paint)
  {
    print_string(0, 480, 150, L"Waiting for", 1);
    print_string(0, 480, 190, L"Other Player", 1);
  }

  ++tickidx;
}

static int random_bubble(struct player_state* p)
{
  int i, j, result;
  int tried = 0;

  while(tried != 0xFF)
  {
    result = rand() % 8;

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

  return rand() % 8;
}

void shoot(struct player_state* p, int color, int velocity)
{
  int i;
  struct moving_bubble mbubble;
  float speed = (velocity == -1) ? bubble_speed : velocity;

  mbubble.falling = 0;
  mbubble.evil = 0;
  mbubble.velx = sin(p->angle / 180.0f * M_PI) * speed;
  mbubble.vely = -cos(p->angle / 180.0f * M_PI) * speed;
  mbubble.x = 112.0f;
  mbubble.y = 350.0f;
  mbubble.color = p->bubble + 1;

  for(i = 0; i < sizeof(p->mbubbles) / sizeof(p->mbubbles[0]); ++i)
  {
    if(p->mbubbles[i].color == 0)
    {
      mbubble.lastpaintx = p->mbubbles[i].lastpaintx;
      mbubble.lastpainty = p->mbubbles[i].lastpainty;
      p->mbubbles[i] = mbubble;

      break;
    }
  }

  p->bubble = p->next_bubble;

  if(p != &players[1])
  {
    if(color == -1)
      p->next_bubble = random_bubble(p);
    else
      p->next_bubble = color;
  }

  if(sound_enable)
    sounds[1].pos = 0;
}

void init_player(struct player_state* p)
{
  int i;

  mark_dirty(p, 0, 0, max_field_width * 32, 440);
  p->last_angle = -1.0f;
  p->angle = 0.0f;
  p->right = 0;
  p->evil_bubble_count = 0;
  p->max_evil = 0;
  p->evil_bubble_seed = 0;

  for(i = 0; i < sizeof(p->mbubbles) / sizeof(p->mbubbles[0]); ++i)
  {
    p->mbubbles[i].color = 0;
    p->mbubbles[i].lastpaintx = INT_MIN;
  }
}

static void init_field()
{
  int i, j, color;

  last_evil = 0;
  for(i = 0; i < field_height / 2; ++i)
  {
    for(j = 0; j < WIDTH(i); ++j)
    {
      color = (rng() % 8) + 1;

      players[0].field[i][j] = color;
      players[1].field[i][j] = color;
    }
  }

  for(; i < field_height; ++i)
  {
    for(j = 0; j < WIDTH(i); ++j)
    {
      players[0].field[i][j] = 0;
      players[1].field[i][j] = 0;
    }
  }

  players[0].bubble = rng() % 8;
  players[0].next_bubble = rng() % 8;
  players[1].bubble = players[0].bubble;
  players[1].next_bubble = players[0].next_bubble;

  init_player(&players[0]);
  init_player(&players[1]);

  input_locked = 0;
  round_logged = 0;
}

void connect_to_master()
{
  int tcp_port = 7170;
  struct hostent* host;
  struct sockaddr_in address;
  struct data_packet packet;

  serverfd = INVALID_SOCKET;
  outbuffer_state = 0;
  inbuffer_state = 0;

  host = gethostbyname("master.junoplay.com");

  if(!host)
    return;

  if(INVALID_SOCKET == (serverfd = socket(host->h_addrtype, SOCK_STREAM, 0)))
    return;

  address.sin_family = host->h_addrtype;
  address.sin_port = htons(tcp_port);
  memcpy(&address.sin_addr, host->h_addr, host->h_length);

  if(SOCKET_ERROR == connect(serverfd, (struct sockaddr*) &address, sizeof(address)))
  {
    close(serverfd);
    serverfd = INVALID_SOCKET;

    return;
  }

  packet.packet_type = packet_identify;
  packet.payload_size = sizeof(packet.identify);

  strcpy(packet.identify.game, "pengupop2007011500");
  memcpy(packet.identify.id, gameid, 32);

  if(listenfd == INVALID_SOCKET)
  {
    packet.identify.port_lo = 0xff;
    packet.identify.port_hi = 0xff;
  }
  else
  {
    packet.identify.port_lo = listenport;
    packet.identify.port_hi = listenport >> 8;
  }

  send_packet(&packet);

#if LINUX || DARWIN
  if(getenv("HOME") && auth_level != 3 && !nologin)
  {
    char confpath[4096];

    strcpy(confpath, getenv("HOME"));
    strcat(confpath, "/.pengupoprc");

    int fd = open(confpath, O_RDONLY);

    if(fd != -1)
    {
      packet.packet_type = packet_login;
      packet.payload_size = sizeof(packet.login);

      if(16 == read(fd, packet.login.username, 16)
      && 16 == read(fd, packet.login.password, 16)
      && packet.login.username[0])
      {
        int i;

        for(i = 0; i < 16; ++i)
          packet.login.password[i] ^= packet.login.username[i];

        send_packet(&packet);

        for(i = 0; i < 16; ++i)
          username[i] = packet.login.username[i];
        username[16] = 0;

        autologin = 1;
      }

      close(fd);
    }
  }
#elif defined(WIN32)
  if(auth_level != 3)
  {
    int i;
    char buf[64];
    DWORD bufsize = sizeof(buf);

    HKEY k_config;

    if(ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Junoplay.com\\Pengupop\\Config", 0, KEY_READ, &k_config))
    {
      if(ERROR_SUCCESS == RegQueryValueEx(k_config, "auth", 0, 0, buf, &bufsize))
      {
        buf[63] = 0;

        packet.packet_type = packet_login;
        packet.payload_size = sizeof(packet.login);
        sscanf(buf, "%[^:]:%s", packet.login.username, packet.login.password);

        send_packet(&packet);

        for(i = 0; i < 16; ++i)
          username[i] = packet.login.username[i];
        username[16] = 0;

        autologin = 1;
      }
    }
  }
#endif
}

void start_listening()
{
  if(-1 == (listenfd = socket(PF_INET, SOCK_STREAM, 0)))
    return;

  for(listenport = 7171; listenport < 7180; ++listenport)
  {
    struct sockaddr_in address;

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(listenport);

    if(SOCKET_ERROR == bind(listenfd, (struct sockaddr*) &address,
                            sizeof(address)))
      continue;

    if(SOCKET_ERROR == listen(listenfd, 1))
      break;

    return;
  }

  close(listenfd);
  listenfd = INVALID_SOCKET;
}

void process_packet(struct data_packet* packet)
{
  int i, j, k, l;
  struct sockaddr_in address;

  if(packet->packet_type == packet_start_game)
  {
    if((mode == MODE_GAME && !single_player) || mode == MODE_SCORE)
    {
      close(serverfd);
      serverfd = INVALID_SOCKET;

      wcscpy(message, L"Corrupt network data");

      mode = MODE_ABORT_MESSAGE;

      return;
    }

    event_count = 0;
    peer_version = packet->start_game.peer_version;
    single_player = 0;

    memcpy(outbound_handshake, packet->start_game.seed, 4);
    memcpy(inbound_handshake, packet->start_game.seed, 4);

    is_server = packet->start_game.is_server;

    if(peer_version >= 3)
    {
      if(is_server)
      {
        outbound_handshake[0] ^= 0xAA;
        outbound_handshake[1] ^= 0xAA;
        outbound_handshake[2] ^= 0xAA;
        outbound_handshake[3] ^= 0xAA;
      }
      else
      {
        inbound_handshake[0] ^= 0xAA;
        inbound_handshake[1] ^= 0xAA;
        inbound_handshake[2] ^= 0xAA;
        inbound_handshake[3] ^= 0xAA;
      }
    }

    rng_seed = packet->start_game.seed[0] << 24
             | packet->start_game.seed[1] << 16
             | packet->start_game.seed[2] << 8
             | packet->start_game.seed[3];

    SDL_BlitSurface(background, 0, screen, 0);

    mode = MODE_GAME;

    init_field();
    players[0].score = 0;
    players[1].score = 0;
    players[0].ready = 1;
    players[1].ready = 1;

    last_tick = SDL_GetTicks();
    tickidx = 0;
    game_tick(1);

    outbufferop_state = 0;
    inbufferop_state = 0;
    outbufferip_state = 0;
    inbufferip_state = 0;
    outpeervalid = 0;
    inpeervalid = 0;

    if(packet->start_game.port_lo == 0xff
    && packet->start_game.port_hi == 0xff)
      return;

    if(INVALID_SOCKET == (outpeerfd = socket(PF_INET, SOCK_STREAM, 0)))
      return;

    /* We are connecting asynchronously to avoid deadlock */
    outpeerconnected = 0;

#ifndef WIN32
    if(-1 == fcntl(outpeerfd, F_SETFL, O_NONBLOCK))
    {
      close(outpeerfd);
      outpeerfd = INVALID_SOCKET;

      return;
    }
#else
    u_long one = 1;

    if(SOCKET_ERROR == ioctlsocket(outpeerfd, FIONBIO, &one))
    {
      close(outpeerfd);
      outpeerfd = INVALID_SOCKET;
    }
#endif

    address.sin_family = AF_INET;
    address.sin_port = htons(packet->start_game.port_lo | packet->start_game.port_hi << 8);
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
    address.sin_addr.s_addr
      = packet->start_game.other_host[0]
      | packet->start_game.other_host[1] << 8
      | packet->start_game.other_host[2] << 16
      | packet->start_game.other_host[3] << 24;
#else
    address.sin_addr.s_addr
      = packet->start_game.other_host[0] << 24
      | packet->start_game.other_host[1] << 16
      | packet->start_game.other_host[2] << 8
      | packet->start_game.other_host[3];
#endif

    if(SOCKET_ERROR == connect(outpeerfd, (struct sockaddr*) &address, sizeof(address)))
    {
#ifndef WIN32
      if(errno != EINPROGRESS)
#else
      if(errno != WSAEWOULDBLOCK)
#endif
      {
        close(outpeerfd);
        outpeerfd = INVALID_SOCKET;
      }
    }

    add_event(tickidx, EVENT_START);
  }
  else if(packet->packet_type == packet_movement)
  {
    if(packet->movement.flags & FLAG_READY)
    {
      players[1].ready = 1;

      if(players[0].ready)
      {
        SDL_BlitSurface(background, 0, screen, 0);

        mode = MODE_GAME;

        last_tick = SDL_GetTicks();
        tickidx = 0;
        game_tick(1);
        add_event(tickidx, EVENT_START);
      }
      else
      {
        chatlog_append(1, L"*** Other player is waiting...  Press enter when you are ready.");
      }
    }

    if(mode != MODE_GAME)
      return;

    players[1].angle = unpack_float(packet->movement.angle);

    if(packet->movement.flags & FLAG_KEY_RIGHT)
      players[1].right = 1;
    else if(packet->movement.flags & FLAG_KEY_LEFT)
      players[1].right = -1;
    else
      players[1].right = 0;

    if(packet->movement.flags & FLAG_BUBBLES)
    {
      k = 0;

      for(i = 0; i < field_height; ++i)
      {
        for(j = 0; j < WIDTH(i); ++j, ++k)
        {
          l = (packet->movement.field[k / 2] >> ((k & 1) ? 4 : 0)) & 0xF;

          if(players[1].field[i][j] != l)
          {
            mark_dirty(&players[1], j * 32 + ((i & 1) ? 16 : 0), i * 28, 32, 32);
            players[1].field[i][j] = l;
          }
        }
      }

      for(i = 0; i < sizeof(players[1].mbubbles) / sizeof(players[1].mbubbles[0]); ++i)
      {
        if(i < packet->movement.mbubble_count)
        {
          players[1].mbubbles[i].x = unpack_float(packet->movement.mbubbles[i].x);
          players[1].mbubbles[i].y = unpack_float(packet->movement.mbubbles[i].y);
          players[1].mbubbles[i].velx = unpack_float(packet->movement.mbubbles[i].velx);
          players[1].mbubbles[i].vely = unpack_float(packet->movement.mbubbles[i].vely);
          players[1].mbubbles[i].falling = packet->movement.mbubbles[i].falling;
          players[1].mbubbles[i].color = packet->movement.mbubbles[i].color;
        }
        else
        {
          players[1].mbubbles[i].color = 0;
        }
      }
    }

    if((packet->movement.flags & FLAG_VICTORY) && !is_server)
    {
      won = 0;
      ++players[1].score;

      players[0].ready = 0;
      players[1].ready = 0;

      log_round();

      mode = MODE_SCORE;

      return;
    }

    if((packet->movement.flags & FLAG_LOSS) && !is_server)
    {
      won = 1;
      ++players[0].score;

      players[0].ready = 0;
      players[1].ready = 0;

      log_round();

      mode = MODE_SCORE;

      return;
    }

    players[1].bubble = packet->movement.bubbles & 7;
    players[1].next_bubble = (packet->movement.bubbles >> 4) & 7;
  }
  else if(packet->packet_type == packet_abort_game)
  {
    int i;

    for(i = 0; i < 32; ++i)
      message[i] = packet->abort_game.reason[i];
    message[32] = 0;

    mode = MODE_ABORT_MESSAGE;
  }
  else if(packet->packet_type == packet_message)
  {
    messageidx = packet->message.idx;

    switch(messageidx)
    {
    case message_login_ok:

      auth_level = 3;

#if LINUX || DARWIN
      if(getenv("HOME") && !autologin)
      {
        char confpath[4096];

        strcpy(confpath, getenv("HOME"));
        strcat(confpath, "/.pengupoprc");

        int fd = open(confpath, O_WRONLY | O_CREAT, 0600);

        if(fd != -1)
        {
          for(i = 0; i < 16; ++i)
          {
            char c = username[i];
            write(fd, &c, 1);
          }

          for(i = 0; i < 16; ++i)
          {
            char c = password[i];
            c ^= username[i];
            write(fd, &c, 1);
          }

          close(fd);
        }
      }
#elif defined(WIN32)
      if(!autologin)
      {
        HKEY k_config;

        if(ERROR_SUCCESS == RegCreateKey(HKEY_CURRENT_USER, "Software\\Junoplay.com\\Pengupop\\Config", &k_config))
        {
          char auth[64];
          snprintf(auth, sizeof(auth), "%ls:%ls", username, password);
          auth[63] = 0;

          RegSetValueEx(k_config, "auth", 0, REG_SZ, auth, strlen(auth));
        }
      }
#endif

      break;

    default:

      mode = MODE_ABORT_MESSAGE2;
    }
  }
  else if(packet->packet_type == packet_chat)
  {
    wchar_t buf[269];
    buf[268] = 0;

    for(i = 0; i < 268; ++i)
      buf[i] = (packet->chat.message[i * 2])
             | (packet->chat.message[i * 2 + 1] << 8);

    chatlog_append(packet->chat.is_private, buf);
  }
  else
  {
    fprintf(stderr, "Received packet of type %u\n", packet->packet_type);
  }
}

void show_splash()
{
  SDL_Event event;

  SDL_BlitSurface(splash, 0, screen, 0);
  SDL_UpdateRect(screen, 0, 0, 0, 0);

  last_tick = SDL_GetTicks();

  while(SDL_GetTicks() - last_tick < 3000)
  {
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

    SDL_UpdateRect(screen, 0, 0, 0, 0);

    Sleep(10);
  }
}

#ifndef WIN32
static void help(const char* argv0)
{
  printf("Usage: %s [OPTION]...\n"
         "Online multiplayer bubble shooting game\n"
         "\n"
         "Mandatory arguments to long options are mandatory for short"
         " options too\n"
         "\n"
         " -n, --no-login             do not log in automatically\n"
         " -w, --windowed             start game in windowed mode\n"
         "     --help     display this help and exit\n"
         "     --version  display version information and exit\n"
         "\n"
         "Report bugs to <morten@rashbox.org>.\n", argv0);
}
#endif

static void join_random(int registered_only)
{
  struct data_packet packet;

  packet.packet_type = packet_join_random_game;
  packet.payload_size = 1;
  packet.join_random_game.registered_only = registered_only;

  if(INVALID_SOCKET == serverfd)
    connect_to_master();

  if(INVALID_SOCKET == serverfd)
  {
    wcscpy(message, L"Failed to Connect to Master Server");

    mode = MODE_ABORT_MESSAGE;

    return;
  }

  send_packet(&packet);

  single_player = 1;

  rng_seed = time(0);

  SDL_BlitSurface(background, 0, screen, 0);

  init_field();
  players[0].score = 0;
  players[1].score = 0;
  players[0].ready = 1;
  players[1].ready = 1;

  last_tick = SDL_GetTicks();
  tickidx = 0;
  game_tick(1);

  mode = MODE_GAME;
}

#ifndef WIN32
int main(int argc, char** argv)
#else
int PASCAL WinMain(HINSTANCE instance, HINSTANCE previnstance,
                   LPSTR cmdline, int cmdshow)
#endif
{
  int i, j, k, selection = 0;
  SDL_Event event;
  SDL_Joystick *joystick;
  SDL_Rect rect;

#ifdef WIN32
  FreeConsole();

  do /* Register `pengupop' URL handler */
  {
    char fname[1024];
    char command[1024];
    HKEY k_pengupop;
    HKEY k_command;

    GetModuleFileName(0, fname, sizeof(fname));
    snprintf(command, sizeof(command) - 1, "\"%s\" %%1", fname);
    command[1023] = 0;

    if(ERROR_SUCCESS != RegCreateKey(HKEY_CLASSES_ROOT, "pengupop", &k_pengupop))
      break;

    RegSetValueEx(k_pengupop, 0, 0, REG_SZ, "URL:Pengupop Protocol", strlen("URL:Pengupop Protocol"));
    RegSetValueEx(k_pengupop, "URL Protocol", 0, REG_SZ, "", 0);

    if(ERROR_SUCCESS != RegCreateKey(HKEY_CLASSES_ROOT, "pengupop\\shell\\open\\command", &k_command))
      break;

    RegSetValueEx(k_command, 0, 0, REG_SZ, command, strlen(command));

    RegCloseKey(k_command);
    RegCloseKey(k_pengupop);
  }
  while(0);

  if(strlen(cmdline) > 6)
    fullscreen = 0;

  WSADATA wsadata;

  WSAStartup(0x0101, &wsadata);
#else
  signal(SIGPIPE, SIG_IGN);
  signal(SIGALRM, SIG_IGN);

  for(;;)
  {
    int optindex = 0;
    int c;

    c = getopt_long(argc, argv, "wn", long_options, &optindex);

    if(c == -1)
      break;

    switch(c)
    {
    case 'n':

      nologin = 1;

      break;

    case 'w':

      fullscreen = 0;

      break;

    case 'h':

      help(argv[0]);

      return EXIT_SUCCESS;

    case 'v':

      printf("%s\n", PACKAGE_STRING);
      printf(
        "Copyright (C) 2006 Junoplay\n"
        "This is free software.  You may redistribute copies of it under the terms of\n"
        "the GNU General Public License <http://www.gnu.org/licenses/gpl.html>.\n"
        "There is NO WARRANTY, to the extent permitted by law.\n"
        "\n"
        "Written by Morten Hustveit.  Artwork by Jorgen Jacobsen.\n");

      return EXIT_SUCCESS;

    case '?':

      fprintf(stderr, "Try `%s --help' for more information.\n", argv[0]);

      return EXIT_FAILURE;
    }
  }
#endif

//  if(-1 == SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
  if(-1 == SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_JOYSTICK))
    fatal_error("SDL initialization failed: %s", SDL_GetError());

  atexit(SDL_Quit);

  SDL_EnableUNICODE(1);

  SDL_JoystickEventState(SDL_ENABLE);
  if (SDL_NumJoysticks() > 0)
      joystick = SDL_JoystickOpen(0);

  screen = SDL_SetVideoMode(width, height, 0, SDL_SWSURFACE | (fullscreen ? SDL_FULLSCREEN : 0));

  if(hwalpha)
    info("Has HW alpha");

  if(!screen)
    fatal_error("Failed to create window: %s", SDL_GetError());

  SDL_WM_SetCaption("Pengupop", 0);
  SDL_ShowCursor(0);

  load_images();
  load_font();

  splash = get_image("splash.png");
  SDL_SetAlpha(splash, 0, 0);

  chat = get_image("chat.png");
  SDL_SetAlpha(chat, 0, 0);

  logo = get_image("logo.png");
  SDL_SetAlpha(logo, 0, 0);

  SDL_BlitSurface(logo, 0, screen, 0);
  SDL_UpdateRect(screen, 0, 0, 0, 0);

  background = get_image("backgrnd.png");
  SDL_SetAlpha(background, 0, 0);

#if LINUX || DARWIN
  if(getenv("HOME"))
  {
    char confpath[4096];

    strcpy(confpath, getenv("HOME"));
    strcat(confpath, "/.pengupoprc");

    int fd = open(confpath, O_RDONLY);

    if(fd != -1)
    {
      lseek(fd, 32, SEEK_SET);

      read(fd, &level, 1);

      if(level >= 201)
        level = 0;

      close(fd);
    }
  }
#elif defined(WIN32)
  {
    char buf[64];
    DWORD bufsize = sizeof(buf);
    HKEY k_config;

    if(ERROR_SUCCESS == RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Junoplay.com\\Pengupop\\Config", 0, KEY_READ, &k_config))
    {
      if(ERROR_SUCCESS == RegQueryValueEx(k_config, "bananas", 0, 0, buf, &bufsize))
      {
        buf[63] = 0;

        int tmp;
        sscanf(buf, "%d", &tmp);
        level = tmp ^ 0x7236143;

        if(level >= 201)
          level = 0;
      }
    }
  }
#endif

  for(i = 0; i < 129; ++i)
  {
    char name[64];

    sprintf(name, "base.png$%d", i);

    base[i] = get_image(name);
  }

  for(i = 0; i < 8; ++i)
  {
    char name[64];

    sprintf(name, "bubble-colourblind-%d.png", i + 1);

    bubbles[i] = get_image(name);
  }

  outpeerfd = INVALID_SOCKET;
  inpeerfd = INVALID_SOCKET;

  start_listening();

#ifdef WIN32
  if(strlen(cmdline) > 6)
  {
    connect_to_master();

    if(serverfd == INVALID_SOCKET)
    {
      wcscpy(message, L"Failed to Connect to Master Server");

      mode = MODE_ABORT_MESSAGE;
    }
    else
    {
      struct data_packet p;

      p.packet_type = packet_create_game;
      p.payload_size = sizeof(p.create_game);

      memset(p.create_game.name, 0, 32);

      for(i = 0; i < 32 && cmdline[i]; ++i)
        p.create_game.name[i] = cmdline[i];

      send_packet(&p);

      wcscpy(message, L"Waiting for Other Player");

      mode = MODE_WAITING;
    }
  }
#endif

  sdl_audio.freq = 44100;
  sdl_audio.format = AUDIO_S16SYS;
  sdl_audio.channels = 1;
  sdl_audio.samples = 1024;
  sdl_audio.callback = sound_callback;
  sdl_audio.userdata = 0;

  if(-1 != SDL_OpenAudio(&sdl_audio, 0))
  {
    load_sounds();

    SDL_PauseAudio(0);
  }
  else
    sdl_audio.freq = 0;

  for(;;)
  {
    Sleep(10);

    int update_movement = 0;
    int has_shot = 0;
    int result;

    if(input_locked && mode == MODE_GAME && (single_player || is_server))
    {
      const int mbubble_count = sizeof(players[0].mbubbles) / sizeof(players[0].mbubbles[0]);

      for(i = 0; i < mbubble_count; ++i)
      {
        if(players[0].mbubbles[i].color)
          break;
      }

      if(i == mbubble_count)
      {
        if(single_player)
        {
          init_field();

          last_tick = SDL_GetTicks();
        }
        else if(is_server)
        {
          for(i = 0; i < mbubble_count; ++i)
          {
            if(players[1].mbubbles[i].color)
              break;
          }

          if(i == mbubble_count)
          {
            struct data_packet packet;

            init_movement_packet(&packet);

            if(won)
            {
              packet.movement.flags |= FLAG_VICTORY;

              ++players[0].score;
            }
            else
            {
              packet.movement.flags |= FLAG_LOSS;

              ++players[1].score;
            }

            send_packet(&packet);

            players[0].ready = 0;
            players[1].ready = 0;

            log_round();

            mode = MODE_SCORE;
          }
        }
      }
    }

    if(mode != MODE_GAME && mode != MODE_SCORE)
    {
      if(inpeerfd != INVALID_SOCKET)
      {
        close(inpeerfd);
        inpeerfd = INVALID_SOCKET;
      }

      if(outpeerfd != INVALID_SOCKET)
      {
        close(outpeerfd);
        outpeerfd = INVALID_SOCKET;
      }
    }

    if(serverfd != INVALID_SOCKET)
    {
      fd_set readset, writeset;
      struct timeval timeout;
#ifndef WIN32
      int maxfd = 0;
#endif

      FD_ZERO(&readset);
      FD_ZERO(&writeset);

      FD_SET(serverfd, &readset);

      if(outbuffer_state)
        FD_SET(serverfd, &writeset);

#ifndef WIN32
      maxfd = serverfd;
#endif

      if(listenfd != INVALID_SOCKET)
      {
        FD_SET(listenfd, &readset);

#ifndef WIN32
        if(listenfd > maxfd)
          maxfd = listenfd;
#endif
      }

      if(outpeerfd != INVALID_SOCKET && mode != MODE_WAITING)
      {
        if(!outpeerconnected)
          FD_SET(outpeerfd, &writeset);
        else
        {
          if(outbufferop_state)
            FD_SET(outpeerfd, &writeset);

          FD_SET(outpeerfd, &readset);
        }

#ifndef WIN32
        if(outpeerfd > maxfd)
          maxfd = outpeerfd;
#endif
      }

      if(inpeerfd != INVALID_SOCKET && mode != MODE_WAITING)
      {
        if(outbufferip_state)
          FD_SET(inpeerfd, &writeset);

        FD_SET(inpeerfd, &readset);

#ifndef WIN32
        if(inpeerfd > maxfd)
          maxfd = inpeerfd;
#endif
      }

      timeout.tv_sec = 0;
      timeout.tv_usec = 0;

#ifndef WIN32
      int count = select(maxfd + 1, &readset, &writeset, 0, &timeout);
#else
      int count = select(0, &readset, &writeset, 0, &timeout);
#endif

      if(count == -1 && errno == EINTR)
        continue;

      if(count == -1)
      {
        close(serverfd);
        serverfd = INVALID_SOCKET;

        wcscpy(message, L"Connection Aborted (select)");

        mode = MODE_ABORT_MESSAGE;

        continue;
      }

      if(FD_ISSET(serverfd, &readset))
      {
        struct data_packet* packet = (struct data_packet*) inbuffer;

        result = recv(serverfd, inbuffer + inbuffer_state, sizeof(inbuffer) - inbuffer_state, 0);

        if(result <= 0)
        {
          close(serverfd);
          serverfd = INVALID_SOCKET;

          wcscpy(message, L"Connection Aborted (read)");

          mode = MODE_ABORT_MESSAGE;

          continue;
        }

        inbuffer_state += result;

        while(inbuffer_state >= 3 && inbuffer_state >= 3 + ntohs(packet->payload_size))
        {
          packet->payload_size = ntohs(packet->payload_size);
          process_packet(packet);

          inbuffer_state -= 3 + packet->payload_size;

          memmove(inbuffer, &inbuffer[3 + packet->payload_size],
                  inbuffer_state);
        }
      }

      if(FD_ISSET(serverfd, &writeset))
      {
        result = send(serverfd, outbuffer, outbuffer_state, 0);

        if(result <= 0)
        {
          close(serverfd);
          serverfd = INVALID_SOCKET;

          wcscpy(message, L"Connection Aborted (write)");

          mode = MODE_ABORT_MESSAGE;

          continue;
        }

        outbuffer_state -= result;
        memmove(outbuffer, outbuffer + result, outbuffer_state);
      }

      if(listenfd != INVALID_SOCKET && FD_ISSET(listenfd, &readset))
      {
        SOCKET newsocket = accept(listenfd, 0, 0);

        if(newsocket != INVALID_SOCKET)
        {
          if(inpeerfd != INVALID_SOCKET)
            close(newsocket);
          else
          {
            inpeerfd = newsocket;

            memcpy(outbufferip, outbound_handshake, sizeof(outbound_handshake));
            outbufferip_state = sizeof(outbound_handshake);
          }
        }
      }

      if(outpeerfd != INVALID_SOCKET && FD_ISSET(outpeerfd, &writeset))
      {
        if(!outpeerconnected)
        {
          outpeerconnected = 1;

          memcpy(outbufferop, outbound_handshake, sizeof(outbound_handshake));
          outbufferop_state = sizeof(outbound_handshake);
        }

        if(outbufferop_state)
        {
          result = send(outpeerfd, outbufferop, outbufferop_state, 0);

          if(result <= 0)
          {
            close(outpeerfd);
            outpeerfd = INVALID_SOCKET;

            continue;
          }

          outbufferop_state -= result;
          memmove(outbuffer, outbufferop + result, outbufferop_state);
        }
      }

      if(inpeerfd != INVALID_SOCKET && outbufferip_state && FD_ISSET(inpeerfd, &writeset))
      {
        result = send(inpeerfd, outbufferip, outbufferip_state, 0);

        if(result <= 0)
        {
          close(inpeerfd);
          inpeerfd = INVALID_SOCKET;

          continue;
        }

        outbufferip_state -= result;
        memmove(outbuffer, outbufferip + result, outbufferip_state);
      }

      if(outpeerfd != INVALID_SOCKET && FD_ISSET(outpeerfd, &readset))
      {
        struct data_packet* packet = (struct data_packet*) inbufferop;

        result = recv(outpeerfd, inbufferop + inbufferop_state,
                      sizeof(inbufferop) - inbufferop_state, 0);

        if(result <= 0)
        {
          close(outpeerfd);
          outpeerfd = INVALID_SOCKET;

          continue;
        }

        inbufferop_state += result;

        if(!outpeervalid)
        {
          if(inbufferop_state >= 4)
          {
            if(memcmp(inbufferop, inbound_handshake, 4))
            {
              close(outpeerfd);
              outpeerfd = INVALID_SOCKET;

              continue;
            }
            else
            {
              outpeervalid = 1;
              inbufferop_state -= 4;
              memmove(inbufferop, inbufferop + 4, inbufferop_state);
            }
          }
        }

        if(outpeervalid)
        {
          while(inbufferop_state >= 3 && inbufferop_state >= 3 + ntohs(packet->payload_size))
          {
            packet->payload_size = ntohs(packet->payload_size);
            process_packet(packet);

            inbufferop_state -= 3 + packet->payload_size;

            memmove(inbufferop, &inbufferop[3 + packet->payload_size],
                    inbufferop_state);
          }
        }
      }

      if(inpeerfd != INVALID_SOCKET && FD_ISSET(inpeerfd, &readset))
      {
        struct data_packet* packet = (struct data_packet*) inbufferip;

        result = recv(inpeerfd, inbufferip + inbufferip_state,
                      sizeof(inbufferip) - inbufferip_state, 0);

        if(result <= 0)
        {
          close(inpeerfd);
          inpeerfd = INVALID_SOCKET;

          continue;
        }

        inbufferip_state += result;

        if(!inpeervalid)
        {
          if(inbufferip_state >= 4)
          {
            if(memcmp(inbufferip, inbound_handshake, 4))
            {
              close(inpeerfd);
              inpeerfd = INVALID_SOCKET;

              continue;
            }
            else
            {
              inpeervalid = 1;
              inbufferip_state -= 4;
              memmove(inbufferip, inbufferip + 4, inbufferip_state);
            }
          }
        }

        if(inpeervalid)
        {
          while(inbufferip_state >= 3 && inbufferip_state >= 3 + ntohs(packet->payload_size))
          {
            packet->payload_size = ntohs(packet->payload_size);
            process_packet(packet);

            inbufferip_state -= 3 + packet->payload_size;

            memmove(inbufferip, &inbufferip[3 + packet->payload_size],
                    inbufferip_state);
          }
        }
      }
    }

    if(mode == MODE_ABORT_MESSAGE || mode == MODE_WAITING)
    {
      Sleep(30);

      rect.x = 0;
      rect.y = 0;
      rect.w = 640;
      rect.h = 480;

      SDL_BlitSurface(logo, &rect, screen, &rect);

      print_string(0, 320, 250, message, 1);
    }
    else if(mode == MODE_ABORT_MESSAGE2)
    {
      Sleep(30);

      rect.x = 0;
      rect.y = 0;
      rect.w = 640;
      rect.h = 480;

      SDL_BlitSurface(logo, &rect, screen, &rect);

      if(messageidx == message_unknown_user)
      {
        auth_level = 0;
        print_string(0, 320, 250, L"You entered an unknown", 1);
        print_string(0, 320, 290, L"user name. Please register at", 1);
        print_string(0, 320, 330, L"http://www.junoplay.com/sign_up", 1);
        print_string(0, 320, 370, L"(Press Escape)", 1);
      }
      else if(messageidx == message_incorrect_password)
      {
        auth_level = 1;
        print_string(0, 320, 250, L"Incorrect password.", 1);
        print_string(0, 320, 290, L"(Press Escape)", 1);
      }
    }
    else if(mode == MODE_MODE_SELECT)
    {
      int bump = (int) fabsf(sinf(last_tick * 0.2f) * 10.0f) - 5;
      ++last_tick;

      Sleep(15);

      rect.x = 0;
      rect.y = 200;
      rect.w = 640;
      rect.h = 260;

      SDL_BlitSurface(logo, &rect, screen, &rect);

      print_string(0, 320 + (selection == 0 ? bump : 0), 250, L"Single Player", 1);
      //print_string(0, 320 + (selection == 1 ? bump : 0), 290, L"Online Multiplayer", 1);
      print_string(0, 320 + (selection == 1 ? bump : 0), 290, L"Multiplayer (Not Available)", 1);
      print_string(0, 320 + (selection == 2 ? bump : 0), 330, L"Quit", 1);
    }
    else if(mode == MODE_SINGLEPLAYER_MENU)
    {
      int bump = (int) fabsf(sinf(last_tick * 0.2f) * 10.0f) - 5;
      ++last_tick;

      Sleep(15);

      rect.x = 0;
      rect.y = 200;
      rect.w = 640;
      rect.h = 260;

      SDL_BlitSurface(logo, &rect, screen, &rect);

      print_string(0, 320 + (selection == 0 ? bump : 0), 250, L"Adventure Mode", 1);
      print_string(0, 320 + (selection == 1 ? bump : 0), 290, L"Infinite Mode", 1);
      print_string(0, 320 + (selection == 2 ? bump : 0), 330, L"Quit", 1);
    }
    else if(mode == MODE_MAIN_MENU)
    {
      int bump = (int) fabsf(sinf(last_tick * 0.2f) * 10.0f) - 5;
      ++last_tick;

      Sleep(15);

      rect.x = 0;
      rect.y = 200;
      rect.w = 640;
      rect.h = 260;

      SDL_BlitSurface(logo, &rect, screen, &rect);

      print_string(0, 320 + (selection == 0 ? bump : 0), 250, L"Join Random Game", 1);
      print_string(0, 320 + (selection == 1 ? bump : 0), 290, L"Enter Lounge", 1);
      print_string(0, 320 + (selection == 2 ? bump : 0), 330, L"Quit", 1);

      if(auth_level != 3)
      {
        print_string(1, 320, 410, L"Note: You must Enter Lounge if you want score statistics to be collected.", 1);
      }
      else
      {
        wchar_t buf[256];

        print_string(0, 320 + (selection == 3 ? bump : 0), 370, L"Log Out", 1);
#ifndef WIN32
        swprintf(buf, sizeof(buf), L"Logged in as %ls.", username);
#else
        swprintf(buf, L"Logged in as %ls.", username);
#endif

        print_string(1, 320, 410, buf, 1);
      }
    }
    else if(mode == MODE_LOUNGE || mode == MODE_SCORE)
    {
      Sleep(30);

      Uint32 now = SDL_GetTicks();

      if(repeat_key && now > repeat_time)
      {
        if(auth_level == 3 || mode == MODE_SCORE)
        {
          if(repeat_key == '\b')
          {
            message_backspace();
          }
          else if(repeat_key == '\v')
          {
            message_delete();
          }
          else if(repeat_key == '\n')
          {
            cursor_left();
          }
          else if(repeat_key == '\r')
          {
            cursor_right();
          }
          else
          {
            message_insert(repeat_key);
          }
        }

        repeat_time = now + 30;
      }

      rect.x = 0;
      rect.y = 0;
      rect.w = 640;
      rect.h = 480;

      SDL_BlitSurface(chat, &rect, screen, &rect);

      if(auth_level == 0 && mode == MODE_LOUNGE)
      {
        print_string(0, 320, 250, L"Enter User Name:", 1);
        print_string(0, 320, 290, username, 1);
      }
      else if(auth_level == 1 && mode == MODE_LOUNGE)
      {
        wchar_t tmp[33];
        int i;

        print_string(0, 320, 250, L"Enter Password:", 1);

        for(i = 0; password[i] && i < 32; ++i)
          tmp[i] = '*';
        tmp[i] = 0;

        print_string(0, 320, 290, tmp, 1);
      }
      else if(auth_level == 2 && mode == MODE_LOUNGE)
      {
        print_string(0, 320, 270, L"Checking Password...", 1);
      }
      else if(auth_level == 3 || mode == MODE_SCORE)
      {
        wchar_t buf[320];

        if(mode == MODE_LOUNGE)
        {
          for(i = 0; i < CHAT_LINES; ++i)
            print_string(1, 10, 129 + i * 17, chatlog[(i + chatlogpos) % CHAT_LINES], 0);
        }
        else
        {
          for(i = 0; i < CHAT_LINES; ++i)
            print_string(1, 10, 129 + i * 17, privchatlog[(i + privchatlogpos) % CHAT_LINES], 0);
        }

        size_t length = message_length();
        size_t off = 0;

        if (message_cursor > length)
          message_cursor = length;

        if(string_width(1, message, length) > CHAT_WIDTH)
        {
          for(off = 1; off < length; ++off)
            if(string_width(1, message + off, length - off) < CHAT_WIDTH)
              break;
        }
        // Insert cursor (or space to make it blinking) :
        memmove(message+message_cursor+1,
                message+message_cursor,
                sizeof(message[0])*(length-message_cursor+1));
        if(SDL_GetTicks() % 500 < 400)
          message[message_cursor] = L'|';
        else
          message[message_cursor] = L' ';
#ifndef WIN32
          swprintf(buf, sizeof(buf), L"%ls", message + off);
#else
          swprintf(buf, L"%ls", message + off);
#endif
        // Remove inserted cursor :
        memmove(message+message_cursor,
                message+message_cursor+1,
                sizeof(message[0])*(length-message_cursor+1));

        if(off != 0)
          print_string(1, 2, 455, L"..", 0);
        print_string(1, 10, 455, buf, 0);
      }
    }
    else if(mode == MODE_GAME)
    {
      Uint32 now = SDL_GetTicks();

      while(now >= last_tick + time_stepms)
      {
        /* Skip delays larger than 5s */
        if(now - last_tick > 5000)
          last_tick = now - 5000;

        last_tick += time_stepms;

        game_tick(now - last_tick < time_stepms);
      }
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
          if(mode == MODE_MODE_SELECT)
          {
            if(selection == 0)
            {
              mode = MODE_SINGLEPLAYER_MENU;
            }
            else if(selection == 1)
            {
             mode = MODE_MODE_SELECT;
            }
            else if(selection == 2)
            {
              exit(EXIT_SUCCESS);
            }
          }
          else if(mode == MODE_SINGLEPLAYER_MENU)
          {
            if(selection == 0)
            {
              saved_level = level;
              play_single_player();

              SDL_BlitSurface(logo, 0, screen, 0);
            }
            else if(selection == 1)
            {
              saved_level = level;
              level = 255;
              play_single_player();

              SDL_BlitSurface(logo, 0, screen, 0);
              selection = 0;
            }
            else if(selection == 2)
            {
              exit(EXIT_SUCCESS);
            }
          }
          else if(mode == MODE_MAIN_MENU)
          {
            if(selection == 0)
            {
              join_random(0);
            }
            else if(selection == 1)
            {
              if(auth_level < 3)
              {
                auth_level = 0;
                username[0] = 0;
                password[0] = 0;
              }

              mode = MODE_LOUNGE;
            }
            else if(selection == 2)
            {
              exit(EXIT_SUCCESS);
            }
            else if(selection == 3)
            {
              struct data_packet p;
              p.packet_type = packet_logout;
              p.payload_size = 0;

              send_packet(&p);

              auth_level = 0;
              selection = 0;
            }
          }
          else if(mode == MODE_GAME && event.key.keysym.sym != SDLK_RETURN)
          {
            if(!has_shot && !input_locked)
            {
              update_movement = 1;
              has_shot = 1;
              shoot(&players[0], -1, -1);
              add_event(tickidx, EVENT_SHOOT | players[0].next_bubble);
            }
          }
          else if(mode == MODE_ABORT_MESSAGE)
          {
            message[0] = 0;
            mode = MODE_MAIN_MENU;
          }           
         }
       if (event.jbutton.button  == 13 || event.jbutton.button == 2)/* Code same as SDLK_ESCAPE */
         {
          if(mode == MODE_MAIN_MENU || mode == MODE_MODE_SELECT || mode == MODE_SINGLEPLAYER_MENU)
          {
            exit(EXIT_SUCCESS);
          }
          else
          {
            submit_events();

            single_player = 0;

            if(mode == MODE_WAITING || mode == MODE_SCORE || mode == MODE_GAME)
            {
              struct data_packet p;

              p.packet_type = packet_abort_game;
              p.payload_size = 0;

              send_packet(&p);
            }

            SDL_BlitSurface(logo, 0, screen, 0);

            mode = MODE_MAIN_MENU;

            message[0] = 0;
          }
         }
      break;

      case SDL_JOYAXISMOTION:
      if ((event.jaxis.value > -3200) || (event.jaxis.value < 3200))  
         {
          switch (event.jaxis.axis)
             {
              case 0:
               if (event.jaxis.value > -3200)/* Code same as SDL_KEYUP:SDLK_LEFT */
                 {
                   if(players[0].right == -1)
                     {
                       update_movement = 1;
                       players[0].right = 0;
                       add_event(tickidx, EVENT_LEFT | EVENT_RIGHT);
                     }
                 }
               else if (event.jaxis.value < -3200)/* Code same as SDL_KEYUP:SDLK_RIGHT */    
                 {
                   if(players[0].right == 1)
                     {
                       update_movement = 1;
                       players[0].right = 0;
                       add_event(tickidx, EVENT_LEFT | EVENT_RIGHT);
                     }
                 }
                break;
            }
         }

      if ((event.jaxis.value < -3200) || (event.jaxis.value > 3200))  
         {
          switch (event.jaxis.axis)
             {
              case 0:
               if (event.jaxis.value < -3200)/* Code same as SDL_KEYDOWN:SDLK_LEFT */
                  {
                    if(mode == MODE_GAME)
                      {
                        update_movement = 1;
                        players[0].right = -1;
                        add_event(tickidx, EVENT_LEFT);
                      }
                    else if(mode == MODE_LOUNGE || mode == MODE_SCORE)
                      {
                        cursor_left();
                        repeat_sym = event.key.keysym.sym;
                        repeat_key = '\n';
                        repeat_time = SDL_GetTicks() + 250;
                      }
                  }
              if (event.jaxis.value > 3200)/* Code same as SDL_KEYDOWN:SDLK_RIGHT */
                  {
                    if(mode == MODE_GAME)
                      {
                        update_movement = 1;
                        players[0].right = 1;
                        add_event(tickidx, EVENT_RIGHT);
                      }
                    else if(mode == MODE_LOUNGE || mode == MODE_SCORE)
                     {
                       cursor_right();
                       repeat_sym = event.key.keysym.sym;
                       repeat_key = '\r';
                       repeat_time = SDL_GetTicks() + 250;
                     }
                  }
  
              break;

              case 1:
          
               if (event.jaxis.value < -3200)/* Code same as SDL_KEYDOWN:SDLK_UP */
                  {
                    if(mode == MODE_MODE_SELECT || mode == MODE_SINGLEPLAYER_MENU)
                      {
                        last_tick = 0;

                        if(selection == 0)
                          selection = 2;
                        else
                          --selection;
                      }
                    else if(mode == MODE_MAIN_MENU)
                      {
                        last_tick = 0;

                        if(selection == 0)
                          selection = (auth_level == 3) ? 3 : 2;
                        else
                          --selection;
                      }
                    else if(mode == MODE_GAME)
                      {
                        if(!has_shot && !input_locked)
                          {
                            update_movement = 1;
                            has_shot = 1;
                            shoot(&players[0], -1, -1);
                            add_event(tickidx, EVENT_SHOOT | players[0].next_bubble);
                          }
                      }
                  }
              if (event.jaxis.value > 3200)/* Code same as SDL_KEYDOWN:SDLK_DOWN */
                  {
                    if(mode == MODE_MODE_SELECT || mode == MODE_SINGLEPLAYER_MENU)
                      {
                        last_tick = 0;

                        if(selection == 2)
                          selection = 0;
                        else
                          ++selection;
                      }
                    else if(mode == MODE_MAIN_MENU)
                      {
                        last_tick = 0;

                        if(selection == ((auth_level == 3) ? 3 : 2))
                          selection = 0;
                        else
                          ++selection;
                      }
                  }
              break;  
             }
         }


      break;

      case SDL_KEYDOWN:

        if(event.key.keysym.unicode >= 32
        && has_char(0, event.key.keysym.unicode)
        && (mode == MODE_LOUNGE || mode == MODE_SCORE)
        && !(event.key.keysym.mod & (KMOD_LALT | KMOD_RALT)))
        {
          int length = 0;

          if(auth_level == 0 && mode == MODE_LOUNGE)
          {
            while(length < 16 && username[length])
              ++length;

            if(length < 16)
            {
              username[length] = event.key.keysym.unicode;
              username[length + 1] = 0;
            }
          }
          else if(auth_level == 1 && mode == MODE_LOUNGE)
          {
            while(length < 16 && password[length])
              ++length;

            if(length < 16)
            {
              password[length] = event.key.keysym.unicode;
              password[length + 1] = 0;
            }
          }
          else if(auth_level == 3 || mode == MODE_SCORE)
          {
            message_insert(event.key.keysym.unicode);
            repeat_sym = event.key.keysym.sym;
            repeat_key = event.key.keysym.unicode;
            repeat_time = SDL_GetTicks() + 250;
            }
          continue;
        }

        switch(event.key.keysym.sym)
        {
        case SDLK_q:
        case SDLK_ESCAPE:

          if(mode == MODE_MAIN_MENU || mode == MODE_MODE_SELECT || mode == MODE_SINGLEPLAYER_MENU)
            exit(EXIT_SUCCESS);
          else
          {
            submit_events();

            single_player = 0;

            if(mode == MODE_WAITING || mode == MODE_SCORE || mode == MODE_GAME)
            {
              struct data_packet p;

              p.packet_type = packet_abort_game;
              p.payload_size = 0;

              send_packet(&p);
            }

            SDL_BlitSurface(logo, 0, screen, 0);

            mode = MODE_MAIN_MENU;

            message[0] = 0;
          }

          break;

        case SDLK_RETURN:

          if(mode == MODE_LOUNGE || mode == MODE_SCORE)
          {
            int i;
            struct data_packet p;

            if(auth_level == 0 && mode == MODE_LOUNGE)
            {
              auth_level = 1;
            }
            else if(auth_level == 1 && mode == MODE_LOUNGE)
            {
              p.packet_type = packet_login;
              p.payload_size = sizeof(p.login);

              for(i = 0; i < 16; ++i)
                p.login.username[i] = username[i];

              for(i = 0; i < 16; ++i)
                p.login.password[i] = password[i];

              if(INVALID_SOCKET == serverfd)
                connect_to_master();

              if(INVALID_SOCKET == serverfd)
              {
                wcscpy(message, L"Failed to Connect to Master Server");

                mode = MODE_ABORT_MESSAGE;

                break;
              }

              send_packet(&p);
              autologin = 0;

              message[0] = 0;

              auth_level = 2;
            }
            else if(auth_level == 3 || mode == MODE_SCORE)
            {
              struct data_packet p;

              if((!wcscmp(message, L"/rand") || !wcscmp(message, L"/go")) && mode == MODE_LOUNGE)
              {
                join_random(0);

                break;
              }

              if((!wcscmp(message, L"/randr") || !wcscmp(message, L"/goreg")) && mode == MODE_LOUNGE)
              {
                join_random(1);

                break;
              }

              if((!wcscmp(message, L"/go") || !message[0]) && mode == MODE_SCORE)
              {
                if(!players[0].ready)
                {
                  players[0].ready = 1;

                  init_field();

                  init_movement_packet(&p);

                  p.movement.flags |= FLAG_READY;

                  send_packet(&p);

                  if(players[1].ready)
                  {
                    SDL_BlitSurface(background, 0, screen, 0);

                    mode = MODE_GAME;

                    last_tick = SDL_GetTicks();
                    tickidx = 0;
                    game_tick(1);
                    add_event(tickidx, EVENT_START);
                  }
                  else
                  {
                    chatlog_append(1, L"*** Waiting for other player...");
                  }
                }

                message[0] = 0;

                break;
              }

              if(!message[0])
                break;

              p.packet_type = packet_chat;
              p.payload_size = sizeof(p.chat);

              if(mode == MODE_SCORE)
              {
                wchar_t buf[270];
#ifndef WIN32
                swprintf(buf, sizeof(buf), L"You: %ls", message);
#else
                swprintf(buf, L"You: %ls", message);
#endif
                chatlog_append(1, buf);

                p.chat.is_private = 1;
              }
              else
                p.chat.is_private = 0;

              for(i = 0; i < 256; ++i)
              {
                p.chat.message[i * 2] = message[i] & 0xFF;
                p.chat.message[i * 2 + 1] = (message[i] >> 8) & 0xFF;
              }

              send_packet(&p);

              message[0] = 0;
            }

            break;
          }

          /* Fall through, used for shooting */
        case SDLK_x:
        case SDLK_SPACE:

          if(mode == MODE_MODE_SELECT)
          {
            if(selection == 0)
            {
              mode = MODE_SINGLEPLAYER_MENU;
            }
            else if(selection == 1)
            {
              mode = MODE_MODE_SELECT;
              /*selection = 0;

              mode = MODE_MAIN_MENU;
              time_stepms = 8;
              time_step = 0.008f;

              connect_to_master();*/
            }
            else if(selection == 2)
            {
              exit(EXIT_SUCCESS);
            }
          }
          else if(mode == MODE_SINGLEPLAYER_MENU)
          {
            if(selection == 0)
            {
              saved_level = level;
              play_single_player();

              SDL_BlitSurface(logo, 0, screen, 0);
            }
            else if(selection == 1)
            {
              saved_level = level;
              level = 255;
              play_single_player();

              SDL_BlitSurface(logo, 0, screen, 0);
              selection = 0;
            }
            else if(selection == 2)
            {
              exit(EXIT_SUCCESS);
            }
          }
          else if(mode == MODE_MAIN_MENU)
          {
            if(selection == 0)
            {
              join_random(0);
            }
            else if(selection == 1)
            {
              if(auth_level < 3)
              {
                auth_level = 0;
                username[0] = 0;
                password[0] = 0;
              }

              mode = MODE_LOUNGE;
            }
            else if(selection == 2)
              exit(EXIT_SUCCESS);
            else if(selection == 3)
            {
              struct data_packet p;
              p.packet_type = packet_logout;
              p.payload_size = 0;

              send_packet(&p);

              auth_level = 0;
              selection = 0;
            }
          }
          else if(mode == MODE_GAME && event.key.keysym.sym != SDLK_RETURN)
          {
            if(!has_shot && !input_locked)
            {
              update_movement = 1;
              has_shot = 1;
              shoot(&players[0], -1, -1);
              add_event(tickidx, EVENT_SHOOT | players[0].next_bubble);
            }
          }
          else if(mode == MODE_ABORT_MESSAGE)
          {
            message[0] = 0;
            mode = MODE_MAIN_MENU;
          }

          break;

        case SDLK_UP:

          if(mode == MODE_MODE_SELECT || mode == MODE_SINGLEPLAYER_MENU)
          {
            last_tick = 0;

            if(selection == 0)
              selection = 2;
            else
              --selection;
          }
          else if(mode == MODE_MAIN_MENU)
          {
            last_tick = 0;

            if(selection == 0)
              selection = (auth_level == 3) ? 3 : 2;
            else
              --selection;
          }
          else if(mode == MODE_GAME)
          {
            if(!has_shot && !input_locked)
            {
              update_movement = 1;
              has_shot = 1;
              shoot(&players[0], -1, -1);
              add_event(tickidx, EVENT_SHOOT | players[0].next_bubble);
            }
          }

          break;

        case SDLK_DOWN:

          if(mode == MODE_MODE_SELECT || mode == MODE_SINGLEPLAYER_MENU)
          {
            last_tick = 0;

            if(selection == 2)
              selection = 0;
            else
              ++selection;
          }
          else if(mode == MODE_MAIN_MENU)
          {
            last_tick = 0;

            if(selection == ((auth_level == 3) ? 3 : 2))
              selection = 0;
            else
              ++selection;
          }

          break;

        case SDLK_LEFT:
          if(mode == MODE_GAME)
          {
            update_movement = 1;
            players[0].right = -1;
            add_event(tickidx, EVENT_LEFT);
          }
          else if(mode == MODE_LOUNGE || mode == MODE_SCORE)
          {
            cursor_left();
            repeat_sym = event.key.keysym.sym;
            repeat_key = '\n';
            repeat_time = SDL_GetTicks() + 250;
          }
          break;

        case SDLK_RIGHT:
          if(mode == MODE_GAME)
          {
            update_movement = 1;
            players[0].right = 1;
            add_event(tickidx, EVENT_RIGHT);
          }
          else if(mode == MODE_LOUNGE || mode == MODE_SCORE)
          {
            cursor_right();
            repeat_sym = event.key.keysym.sym;
            repeat_key = '\r';
            repeat_time = SDL_GetTicks() + 250;
          }
          break;

        case SDLK_BACKSPACE:
          if(mode == MODE_LOUNGE || mode == MODE_SCORE)
          {
            int length = 0;

            if(auth_level == 0 && mode == MODE_LOUNGE)
            {
              while(length < 16 && username[length])
                ++length;

              if(length > 0)
                username[--length] = 0;
            }
            else if(auth_level == 1 && mode == MODE_LOUNGE)
            {
              while(length < 16 && password[length])
                ++length;

              if(length > 0)
                password[--length] = 0;
            }
            else if(auth_level == 3 || mode == MODE_SCORE)
            {
              message_backspace();
            }

            repeat_sym = event.key.keysym.sym;
            repeat_key = '\b';
            repeat_time = SDL_GetTicks() + 250;
          }
          break;

        case SDLK_DELETE:
          if((mode == MODE_LOUNGE && auth_level == 3) || mode == MODE_SCORE)
          {
            message_delete();
            repeat_sym = event.key.keysym.sym;
            repeat_key = '\v';
            repeat_time = SDL_GetTicks() + 250;
          }
          break;

        case SDLK_HOME:
          if((mode == MODE_LOUNGE && auth_level == 3) || mode == MODE_SCORE)
          {
             message_cursor = 0;
          }
          break;

        case SDLK_END:
          if((mode == MODE_LOUNGE && auth_level == 3) || mode == MODE_SCORE)
          {
            message_cursor = message_length();
          }
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

          if(mode != MODE_GAME)
          {
            SDL_BlitSurface(logo, 0, screen, 0);
          }
          else
          {
            SDL_BlitSurface(background, 0, screen, 0);

            for(i = 0; i < 2; ++i)
            {
              players[i].dirty_minx = 0;
              players[i].dirty_miny = 0;
              players[i].dirty_maxx = max_field_width * 32;
              players[i].dirty_maxy = 440;
            }
          }
#endif
          fullscreen = !fullscreen;

          break;

        default:;
        }

        break;

      case SDL_KEYUP:

        if(event.key.keysym.sym == repeat_sym)
          repeat_key = 0;

        switch(event.key.keysym.sym)
        {
        case SDLK_LEFT:

          if(players[0].right == -1)
          {
            update_movement = 1;
            players[0].right = 0;

            add_event(tickidx, EVENT_LEFT | EVENT_RIGHT);
          }

          break;

        case SDLK_RIGHT:

          if(players[0].right == 1)
          {
            update_movement = 1;
            players[0].right = 0;

            add_event(tickidx, EVENT_LEFT | EVENT_RIGHT);
          }

          break;

        default:;
        }

        break;
      }
    }

    if(players[0].evil_bubble_count && ((last_evil + (100 / time_stepms) < tickidx) || peer_version < 3))
    {
      struct player_state* p = &players[0];

      i = 0;

      if(p->evil_bubble_count > 4)
        p->evil_bubble_count = 4;

      while(p->evil_bubble_count && i < sizeof(p->mbubbles) / sizeof(p->mbubbles[0]))
      {
        if(p->mbubbles[i].color != 0 || p->mbubbles[i].lastpaintx != INT_MIN)
        {
          ++i;

          continue;
        }

        float rand = sin(pow(p->evil_bubble_seed++, 4.5)) * 0.5 + 0.5;
        float angle = rand * 60 - 30;

        p->mbubbles[i].falling = 0;
        p->mbubbles[i].velx = sin(angle / 180 * M_PI) * bubble_speed;
        p->mbubbles[i].vely = -cos(angle / 180 * M_PI) * bubble_speed;
        p->mbubbles[i].x = 112.0f;
        p->mbubbles[i].y = 400.0f;
        p->mbubbles[i].color = p->evil_bubbles[0];

        add_event(tickidx, EVENT_EVIL | p->evil_bubbles[0]);

        --p->evil_bubble_count;

        memmove(p->evil_bubbles, &p->evil_bubbles[1], p->evil_bubble_count);

        ++i;

        if(peer_version >= 3)
        {
          last_evil = tickidx;
          break;
        }
      }

      update_movement = 1;
      has_shot = 1;
    }

    if(update_movement)
    {
      struct data_packet packet;

      init_movement_packet(&packet);

      if(has_shot)
      {
        packet.movement.flags |= FLAG_BUBBLES;

        k = 0;

        memset(packet.movement.field, 0, sizeof(packet.movement.field));

        for(i = 0; i < field_height; ++i)
        {
          for(j = 0; j < WIDTH(i); ++j, ++k)
          {
            packet.movement.field[k / 2]
              |= players[0].field[i][j] << ((k & 1) ? 4 : 0);
          }
        }

        packet.movement.mbubble_count = 0;
        packet.payload_size += 54;

        for(i = 0; i < sizeof(players[0].mbubbles) / sizeof(players[0].mbubbles[0]); ++i)
        {
          if(players[0].mbubbles[i].color != 0)
          {
            j = packet.movement.mbubble_count++;
            packet.payload_size += sizeof(struct network_mbubble);

            pack_float(packet.movement.mbubbles[j].x, players[0].mbubbles[i].x);
            pack_float(packet.movement.mbubbles[j].y, players[0].mbubbles[i].y);
            pack_float(packet.movement.mbubbles[j].velx, players[0].mbubbles[i].velx);
            pack_float(packet.movement.mbubbles[j].vely, players[0].mbubbles[i].vely);
            packet.movement.mbubbles[j].falling = players[0].mbubbles[i].falling;
            packet.movement.mbubbles[j].color = players[0].mbubbles[i].color;
          }
        }
      }

      send_packet(&packet);
    }

    SDL_UpdateRect(screen, 0, 0, 0, 0);
  }
}
