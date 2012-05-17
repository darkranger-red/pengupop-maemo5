#ifndef SOUND_H_
#define SOUND_H_ 1

struct sound
{
  SDL_AudioSpec spec;
  Uint8* buf;
  Uint32 len;

  Uint32 pos;
};

extern struct sound sounds[4];
extern SDL_AudioSpec sdl_audio;

void load_sounds();

void SDLCALL sound_callback(void* userdata, Uint8* stream, int len);

#endif /* SOUND_H_ */
