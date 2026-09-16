#ifndef REMOTEOS_MEDIA_H
#define REMOTEOS_MEDIA_H
#include <SDL.h>
#include <stddef.h>
/* Homogeneous clip-space position and linear RGB, three vertices per face. */
typedef struct { double x, y, z, w, r, g, b; } MediaVertex;
int media_render(SDL_Surface *, const MediaVertex *, size_t, Uint32);
const char *media_renderer(void);
void media_cleanup(void);
typedef struct MediaVideo MediaVideo;
MediaVideo *media_video_open(const void *, size_t);
int media_video_next(MediaVideo *, SDL_Surface *, double *);
int media_video_seek(MediaVideo *, double);
void media_video_close(MediaVideo *);
int media_video_width(MediaVideo *);
int media_video_height(MediaVideo *);
int media_video_play(MediaVideo *);
int media_video_pause(MediaVideo *);
int media_video_tick(MediaVideo *, SDL_Surface *, double *, int *);
typedef struct MediaEncoder MediaEncoder;
MediaEncoder *media_encoder_open(int width, int height, int fps, int audio);
int media_encoder_frame(MediaEncoder *, SDL_Surface *, const void *, size_t);
int media_encoder_finish(MediaEncoder *);
const unsigned char *media_encoder_data(MediaEncoder *, size_t *);
void media_encoder_close(MediaEncoder *);
#endif
