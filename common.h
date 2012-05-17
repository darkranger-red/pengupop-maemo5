struct moving_bubble
{
  float x;
  float y;
  float velx;
  float vely;

  int lastpaintx;
  int lastpainty;
  int evil;

  unsigned char falling;
  unsigned char color;
};

struct player_state
{
  unsigned char field[14][16];

  int bubble;
  int next_bubble;

  float angle;
  int right;

  int dirty_minx;
  int dirty_miny;
  int dirty_maxx;
  int dirty_maxy;
  float last_angle;

  int score;
  int ready;

  struct moving_bubble mbubbles[256];

  unsigned char evil_bubbles[128];
  int evil_bubble_count;
  int max_evil;
  unsigned int evil_bubble_seed;
};

extern int time_stepms;
extern float time_step;
static const int field_height = 14;
static const int max_field_width = 8;
static const float bubble_speed = 500;
static const int max_x = 414 - 190;
#define WIDTH(y) (((y) & 1) ? 7 : 8)

#define SET_RECT(r, xv, yv, wv, hv) do { (r).x = (xv); (r).y = (yv); (r).w = (wv); (r).h = (hv); } while(0)

extern SDL_Surface* screen;

extern SDL_Surface* logo;
extern SDL_Surface* background;
extern SDL_Surface* base[129];
extern SDL_Surface* bubbles[8];

static const int width = 640;
static const int height = 480;
extern int fullscreen;
extern int sound_enable;

extern unsigned int rng_seed;

int stick(struct player_state* p, int bx, int by, int color);
void mark_dirty(struct player_state* p, int x, int y, int width, int height);
void cond_blit(struct player_state* p, SDL_Surface* source, SDL_Rect* source_rect, SDL_Surface* dest, SDL_Rect* dest_rect);
void show_splash();
void shoot(struct player_state* p, int color, int velocity);
void init_player(struct player_state* p);

unsigned int rng();

void load_images();
SDL_Surface* get_image(const char* name);

void load_font();
int has_char(int font, int ch);
void print_string(int font, int x, int y, const wchar_t* string, int align);
int string_width(int font, const wchar_t* string, size_t length);
