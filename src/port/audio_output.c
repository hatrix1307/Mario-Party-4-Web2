// Real audio output device for the software MusyX mixer (extern/musyx's
// hw_pc.c) -- opens an SDL3 audio stream and, whenever SDL needs more
// samples, calls pcMixerGenerate() to render them. Under Emscripten, SDL3's
// audio backend routes this through Web Audio automatically.
#include "musyx/hw_pc_mixer.h"
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_init.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef EMSCRIPTEN
#include <emscripten/em_macros.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

static SDL_AudioStream *sStream = NULL;
static int sCallbackCount = 0;

static void SDLCALL AudioOutputCallback(void *userdata, SDL_AudioStream *stream,
                                         int additional_amount, int total_amount) {
  s16 buf[4096];
  int wantBytes = additional_amount;

  (void)userdata;
  (void)total_amount;

  if (sCallbackCount < 5) {
    printf("[audio] callback #%d additional=%d total=%d\n", sCallbackCount, additional_amount,
           total_amount);
    sCallbackCount++;
  }

  while (wantBytes > 0) {
    int frames = wantBytes / (int)sizeof(s16) / 2;
    int chunkFrames = frames;
    if (chunkFrames > (int)(sizeof(buf) / sizeof(buf[0]) / 2)) {
      chunkFrames = (int)(sizeof(buf) / sizeof(buf[0]) / 2);
    }
    if (chunkFrames <= 0) {
      break;
    }
    pcMixerGenerate(buf, (u32)chunkFrames);
    SDL_PutAudioStreamData(stream, buf, chunkFrames * 2 * (int)sizeof(s16));
    wantBytes -= chunkFrames * 2 * (int)sizeof(s16);
  }
}

// Called from JS (Module._AudioOutputInit()) on first user interaction, not
// from the C boot path -- see the comment at HuAudInit()'s call site
// (src/port/audio.c) for why.
EMSCRIPTEN_KEEPALIVE void AudioOutputInit(void) {
  SDL_AudioSpec spec;

  printf("[audio] AudioOutputInit() called\n");
  if (sStream != NULL) {
    printf("[audio] already initialized\n");
    return;
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    printf("[audio] SDL_InitSubSystem(SDL_INIT_AUDIO) failed: %s\n", SDL_GetError());
    return;
  }
  printf("[audio] SDL_INIT_AUDIO ok\n");

  spec.format = SDL_AUDIO_S16;
  spec.channels = 2;
  spec.freq = 32000;

  sStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                       AudioOutputCallback, NULL);
  if (sStream == NULL) {
    printf("[audio] SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
    return;
  }
  printf("[audio] stream opened, resuming\n");
  if (!SDL_ResumeAudioStreamDevice(sStream)) {
    printf("[audio] SDL_ResumeAudioStreamDevice failed: %s\n", SDL_GetError());
  } else {
    printf("[audio] resumed ok\n");
  }
}
