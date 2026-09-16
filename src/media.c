#include "media.h"
#include <SDL_opengl.h>
#include <SDL_opengl_glext.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

static SDL_Window *gl_window;
static SDL_GLContext gl_context;
static const char *renderer = "software-depth";
const char *media_renderer(void) { return renderer; }

/* Hidden native-window framebuffers are undefined on some drivers. Render
 * into explicitly allocated attachments, independent of window visibility. */
static PFNGLGENFRAMEBUFFERSPROC gen_framebuffers;
static PFNGLDELETEFRAMEBUFFERSPROC delete_framebuffers;
static PFNGLBINDFRAMEBUFFERPROC bind_framebuffer;
static PFNGLGENRENDERBUFFERSPROC gen_renderbuffers;
static PFNGLDELETERENDERBUFFERSPROC delete_renderbuffers;
static PFNGLBINDRENDERBUFFERPROC bind_renderbuffer;
static PFNGLRENDERBUFFERSTORAGEPROC renderbuffer_storage;
static PFNGLFRAMEBUFFERRENDERBUFFERPROC framebuffer_renderbuffer;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC check_framebuffer;
static void *gl_proc(const char *core, const char *extension) {
    void *fn=SDL_GL_GetProcAddress(core);
    return fn ? fn : SDL_GL_GetProcAddress(extension);
}
static int load_framebuffer_api(void) {
    gen_framebuffers=(PFNGLGENFRAMEBUFFERSPROC)gl_proc("glGenFramebuffers","glGenFramebuffersEXT");
    delete_framebuffers=(PFNGLDELETEFRAMEBUFFERSPROC)gl_proc("glDeleteFramebuffers","glDeleteFramebuffersEXT");
    bind_framebuffer=(PFNGLBINDFRAMEBUFFERPROC)gl_proc("glBindFramebuffer","glBindFramebufferEXT");
    gen_renderbuffers=(PFNGLGENRENDERBUFFERSPROC)gl_proc("glGenRenderbuffers","glGenRenderbuffersEXT");
    delete_renderbuffers=(PFNGLDELETERENDERBUFFERSPROC)gl_proc("glDeleteRenderbuffers","glDeleteRenderbuffersEXT");
    bind_renderbuffer=(PFNGLBINDRENDERBUFFERPROC)gl_proc("glBindRenderbuffer","glBindRenderbufferEXT");
    renderbuffer_storage=(PFNGLRENDERBUFFERSTORAGEPROC)gl_proc("glRenderbufferStorage","glRenderbufferStorageEXT");
    framebuffer_renderbuffer=(PFNGLFRAMEBUFFERRENDERBUFFERPROC)gl_proc("glFramebufferRenderbuffer","glFramebufferRenderbufferEXT");
    check_framebuffer=(PFNGLCHECKFRAMEBUFFERSTATUSPROC)gl_proc("glCheckFramebufferStatus","glCheckFramebufferStatusEXT");
    return gen_framebuffers && delete_framebuffers && bind_framebuffer && gen_renderbuffers &&
        delete_renderbuffers && bind_renderbuffer && renderbuffer_storage && framebuffer_renderbuffer && check_framebuffer ? 0 : -1;
}

/* Clip before perspective division, including triangles crossing the eye. */
static double plane(MediaVertex v, int p) {
    switch (p) {
    case 0: return v.w + v.x; case 1: return v.w - v.x;
    case 2: return v.w + v.y; case 3: return v.w - v.y;
    case 4: return v.w + v.z; default: return v.w - v.z;
    }
}
static MediaVertex lerp(MediaVertex a, MediaVertex b, double t) {
    return (MediaVertex){a.x+(b.x-a.x)*t, a.y+(b.y-a.y)*t,
        a.z+(b.z-a.z)*t, a.w+(b.w-a.w)*t, a.r+(b.r-a.r)*t,
        a.g+(b.g-a.g)*t, a.b+(b.b-a.b)*t};
}
static double edge(double ax, double ay, double bx, double by, double x, double y) {
    return (x-ax)*(by-ay)-(y-ay)*(bx-ax);
}
static void triangle(SDL_Surface *s, double *depth, MediaVertex a, MediaVertex b, MediaVertex c) {
    MediaVertex v[3] = {a,b,c};
    double x[3], y[3], z[3];
    for (int i=0;i<3;i++) {
        if (v[i].w <= 1e-12) return;
        x[i]=(v[i].x/v[i].w+1)*s->w/2; y[i]=(1-v[i].y/v[i].w)*s->h/2;
        z[i]=v[i].z/v[i].w;
    }
    double area=edge(x[0],y[0],x[1],y[1],x[2],y[2]);
    if (fabs(area)<1e-10) return;
    int xmin=(int)fmax(0,floor(fmin(x[0],fmin(x[1],x[2]))));
    int xmax=(int)fmin(s->w-1,ceil(fmax(x[0],fmax(x[1],x[2]))));
    int ymin=(int)fmax(0,floor(fmin(y[0],fmin(y[1],y[2]))));
    int ymax=(int)fmin(s->h-1,ceil(fmax(y[0],fmax(y[1],y[2]))));
    for (int row=ymin;row<=ymax;row++) for (int col=xmin;col<=xmax;col++) {
        double u=edge(x[1],y[1],x[2],y[2],col+.5,row+.5)/area;
        double t=edge(x[2],y[2],x[0],y[0],col+.5,row+.5)/area;
        double q=1-u-t;
        if (u<0 || t<0 || q<0) continue;
        double d=u*z[0]+t*z[1]+q*z[2]; size_t index=(size_t)row*s->w+col;
        if (d>=depth[index]) continue;
        depth[index]=d;
        u/=a.w; t/=b.w; q/=c.w; double sum=u+t+q;
        Uint8 r=(Uint8)fmin(255,fmax(0,255*(u*a.r+t*b.r+q*c.r)/sum));
        Uint8 g=(Uint8)fmin(255,fmax(0,255*(u*a.g+t*b.g+q*c.g)/sum));
        Uint8 blue=(Uint8)fmin(255,fmax(0,255*(u*a.b+t*b.b+q*c.b)/sum));
        ((Uint32 *)((char *)s->pixels+row*s->pitch))[col]=SDL_MapRGB(s->format,r,g,blue);
    }
}
static int render_gl(SDL_Surface *s, const MediaVertex *v, size_t count, Uint32 clear) {
    if (!gl_window) {
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,0);
        gl_window=SDL_CreateWindow("RemoteOS 3D",0,0,s->w,s->h,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
        if (!gl_window) return -1;
        gl_context=SDL_GL_CreateContext(gl_window);
        if (!gl_context) { SDL_DestroyWindow(gl_window); gl_window=NULL; return -1; }
    }
    if (SDL_GL_MakeCurrent(gl_window,gl_context)) return -1;
    if (load_framebuffer_api()) return -1;
    while (glGetError()!=GL_NO_ERROR) {}
    GLuint framebuffer=0, buffers[2]={0,0};
    int error=1;
    unsigned char *pixels=NULL;
    gen_framebuffers(1,&framebuffer); gen_renderbuffers(2,buffers);
    bind_framebuffer(GL_FRAMEBUFFER,framebuffer);
    bind_renderbuffer(GL_RENDERBUFFER,buffers[0]);
    renderbuffer_storage(GL_RENDERBUFFER,GL_RGBA8,s->w,s->h);
    framebuffer_renderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,buffers[0]);
    bind_renderbuffer(GL_RENDERBUFFER,buffers[1]);
    renderbuffer_storage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,s->w,s->h);
    framebuffer_renderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,buffers[1]);
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    if (check_framebuffer(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE || glGetError()!=GL_NO_ERROR) goto done;
    glViewport(0,0,s->w,s->h); glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_DITHER);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glClearColor(((clear>>16)&255)/255.f,((clear>>8)&255)/255.f,(clear&255)/255.f,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glBegin(GL_TRIANGLES);
    for (size_t i=0;i<count;i++) { glColor3d(v[i].r,v[i].g,v[i].b); glVertex4d(v[i].x,v[i].y,v[i].z,v[i].w); }
    glEnd();
    pixels=malloc((size_t)s->w*s->h*4);
    if (!pixels) goto done;
    glPixelStorei(GL_PACK_ALIGNMENT,1);
    glReadPixels(0,0,s->w,s->h,GL_RGBA,GL_UNSIGNED_BYTE,pixels);
    error=glGetError()!=GL_NO_ERROR;
    if (!error && SDL_LockSurface(s)==0) {
        for (int y=0;y<s->h;y++) for (int x=0;x<s->w;x++) {
            unsigned char *p=pixels+((size_t)(s->h-y-1)*s->w+x)*4;
            ((Uint32 *)((char *)s->pixels+y*s->pitch))[x]=SDL_MapRGB(s->format,p[0],p[1],p[2]);
        }
        SDL_UnlockSurface(s);
    } else error=1;
done:
    free(pixels);
    bind_framebuffer(GL_FRAMEBUFFER,0); bind_renderbuffer(GL_RENDERBUFFER,0);
    delete_renderbuffers(2,buffers); delete_framebuffers(1,&framebuffer);
    if (!error) renderer="opengl-depth";
    return error ? -1 : 0;
}
int media_render(SDL_Surface *s, const MediaVertex *vertices, size_t count, Uint32 clear) {
    if (!s || s->format->BytesPerPixel!=4 || s->w<1 || s->h<1 || s->w>2048 || s->h>2048 || count>49152 || count%3) return -1;
    const char *mode=getenv("REMOTEOS_3D_BACKEND");
    if (mode && strcmp(mode,"opengl") && strcmp(mode,"software")) return -1;
    const char *driver=SDL_GetCurrentVideoDriver();
    if ((mode && !strcmp(mode,"opengl")) || (!mode && driver && strcmp(driver,"dummy"))) {
        if (render_gl(s,vertices,count,clear)==0) return 0;
        if (mode) return -1; /* Explicit backend requests must not silently fall back. */
    }
    renderer="software-depth";
    double *depth=malloc((size_t)s->w*s->h*sizeof(double));
    if (!depth) return -1;
    for (size_t i=0;i<(size_t)s->w*s->h;i++) depth[i]=INFINITY;
    SDL_FillRect(s,NULL,SDL_MapRGB(s->format,clear>>16,(clear>>8)&255,clear&255));
    if (SDL_LockSurface(s)) { free(depth); return -1; }
    for (size_t i=0;i<count;i+=3) {
        MediaVertex a[16], b[16]; int n=3;
        memcpy(a,vertices+i,3*sizeof(*a));
        for (int p=0;p<6 && n;p++) {
            int m=0;
            for (int j=0;j<n;j++) {
                MediaVertex prev=a[(j+n-1)%n], next=a[j]; double d0=plane(prev,p),d1=plane(next,p);
                if ((d0>=0)!=(d1>=0)) b[m++]=lerp(prev,next,d0/(d0-d1));
                if (d1>=0) b[m++]=next;
            }
            n=m; memcpy(a,b,(size_t)n*sizeof(*a));
        }
        for (int j=1;j+1<n;j++) triangle(s,depth,a[0],a[j],a[j+1]);
    }
    SDL_UnlockSurface(s); free(depth); return 0;
}
void media_cleanup(void) {
    if (gl_context) SDL_GL_DeleteContext(gl_context);
    if (gl_window) SDL_DestroyWindow(gl_window);
    gl_context=NULL; gl_window=NULL;
}

struct MediaVideo {
    unsigned char *bytes; size_t size, offset;
    AVFormatContext *format; AVIOContext *io; AVCodecContext *codec;
    AVPacket *packet; AVFrame *frame; struct SwsContext *scale;
    int stream, draining;
    double last_seconds, frame_seconds;
    double origin, position, pending_seconds;
    unsigned char *audio;
    size_t audio_size;
    int audio_loaded, playing, pending_valid, ended;
    SDL_AudioDeviceID audio_device;
    SDL_Surface *pending;
    Uint64 started, drained;
    Uint32 queued;
};
static int player_queue(MediaVideo *v);
static double player_time(MediaVideo *v);
static int video_read(void *opaque, uint8_t *buffer, int size) {
    MediaVideo *v=opaque; size_t n=v->size-v->offset;
    if (!n) return AVERROR_EOF;
    if (n>(size_t)size) n=(size_t)size;
    memcpy(buffer,v->bytes+v->offset,n); v->offset+=n; return (int)n;
}
static int64_t video_seek(void *opaque, int64_t offset, int whence) {
    MediaVideo *v=opaque;
    if (whence==AVSEEK_SIZE) return (int64_t)v->size;
    whence &= ~AVSEEK_FORCE;
    int64_t base=whence==SEEK_SET ? 0 : whence==SEEK_CUR ? (int64_t)v->offset : whence==SEEK_END ? (int64_t)v->size : -1;
    if (base<0 || offset < -base || offset > (int64_t)v->size-base) return AVERROR(EINVAL);
    v->offset=(size_t)(base+offset); return (int64_t)v->offset;
}
void media_video_close(MediaVideo *v) {
    if (!v) return;
    if (v->audio_device) { SDL_CloseAudioDevice(v->audio_device); SDL_QuitSubSystem(SDL_INIT_AUDIO); }
    SDL_FreeSurface(v->pending); free(v->audio);
    sws_freeContext(v->scale); av_frame_free(&v->frame); av_packet_free(&v->packet);
    avcodec_free_context(&v->codec); avformat_close_input(&v->format);
    if (v->io) { av_freep(&v->io->buffer); avio_context_free(&v->io); }
    free(v->bytes); free(v);
}
MediaVideo *media_video_open(const void *bytes, size_t size) {
    if (!size || size>16*1024*1024) return NULL;
    MediaVideo *v=calloc(1,sizeof(*v)); if (!v) return NULL;
    v->bytes=malloc(size); if (!v->bytes) goto fail;
    memcpy(v->bytes,bytes,size); v->size=size;
    unsigned char *buffer=av_malloc(32768); if (!buffer) goto fail;
    v->io=avio_alloc_context(buffer,32768,0,v,video_read,NULL,video_seek);
    if (!v->io) { av_free(buffer); goto fail; }
    v->format=avformat_alloc_context(); if (!v->format) goto fail;
    v->format->pb=v->io; v->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    AVDictionary *options=NULL;
    av_dict_set(&options,"protocol_whitelist","",0);
    av_dict_set(&options,"format_whitelist","mov,matroska,webm,avi,ogg,gif",0);
    int rc=avformat_open_input(&v->format,NULL,NULL,&options); av_dict_free(&options);
    if (rc<0 || avformat_find_stream_info(v->format,NULL)<0) goto fail;
    const AVCodec *decoder=NULL;
    v->stream=av_find_best_stream(v->format,AVMEDIA_TYPE_VIDEO,-1,-1,&decoder,0);
    if (v->stream<0) goto fail;
    v->codec=avcodec_alloc_context3(decoder); if (!v->codec) goto fail;
    if (avcodec_parameters_to_context(v->codec,v->format->streams[v->stream]->codecpar)<0) goto fail;
    if (v->codec->width<1 || v->codec->height<1 || v->codec->width>4096 || v->codec->height>4096) goto fail;
    v->codec->thread_count=2;
    v->codec->max_pixels=4096*4096;
    if (avcodec_open2(v->codec,decoder,NULL)<0) goto fail;
    v->packet=av_packet_alloc(); v->frame=av_frame_alloc();
    if (!v->packet || !v->frame) goto fail;
    AVRational rate=av_guess_frame_rate(v->format,v->format->streams[v->stream],NULL);
    v->frame_seconds=rate.num>0 && rate.den>0 ? av_q2d(av_inv_q(rate)) : 1.0/30;
    v->last_seconds=-1;
    v->origin=v->format->start_time==AV_NOPTS_VALUE ? 0 : v->format->start_time/(double)AV_TIME_BASE;
    return v;
fail: media_video_close(v); return NULL;
}
int media_video_width(MediaVideo *v) { return v->codec->width; }
int media_video_height(MediaVideo *v) { return v->codec->height; }
int media_video_seek(MediaVideo *v, double seconds) {
    if (!isfinite(seconds) || seconds<0 || seconds>86400) return -1;
    int64_t timestamp=(int64_t)((seconds+v->origin)/av_q2d(v->format->streams[v->stream]->time_base));
    if (av_seek_frame(v->format,v->stream,timestamp,AVSEEK_FLAG_BACKWARD)<0) return -1;
    avcodec_flush_buffers(v->codec); v->draining=0; v->last_seconds=seconds-v->frame_seconds;
    v->position=seconds; v->pending_valid=0; v->ended=0;
    if (v->audio_loaded && player_queue(v)<0) return -1;
    return 0;
}
int media_video_next(MediaVideo *v, SDL_Surface *dst, double *seconds) {
    if (!dst || dst->format->BytesPerPixel!=4 || dst->w>4096 || dst->h>4096) return -1;
    for (int budget=0;budget<100000;budget++) {
        int rc=avcodec_receive_frame(v->codec,v->frame);
        if (rc==0) {
            AVFrame *f=v->frame;
            if (f->width<1 || f->height<1 || f->width>4096 || f->height>4096) return -1;
            v->scale=sws_getCachedContext(v->scale,f->width,f->height,f->format,dst->w,dst->h,
                AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
            uint8_t *pixels=malloc((size_t)dst->w*dst->h*4);
            if (!pixels) return -1;
            if (!v->scale || SDL_LockSurface(dst)) { free(pixels); return -1; }
            uint8_t *data[4]={pixels,NULL,NULL,NULL}; int strides[4]={dst->w*4,0,0,0};
            int rows=sws_scale(v->scale,(const uint8_t *const *)f->data,f->linesize,0,f->height,data,strides);
            if (SDL_ConvertPixels(dst->w,dst->h,SDL_PIXELFORMAT_BGRA32,pixels,dst->w*4,
                                  dst->format->format,dst->pixels,dst->pitch)) rows=-1;
            free(pixels);
            SDL_UnlockSurface(dst);
            *seconds=f->best_effort_timestamp==AV_NOPTS_VALUE ? v->last_seconds+v->frame_seconds : f->best_effort_timestamp*av_q2d(v->format->streams[v->stream]->time_base)-v->origin;
            if (*seconds<0) *seconds=0;
            v->last_seconds=*seconds;
            av_frame_unref(f); return rows==dst->h ? 1 : -1;
        }
        if (rc==AVERROR_EOF) return 0;
        if (rc!=AVERROR(EAGAIN) || v->draining) return -1;
        rc=av_read_frame(v->format,v->packet);
        if (rc==AVERROR_EOF) { v->draining=1; if (avcodec_send_packet(v->codec,NULL)<0) return -1; }
        else if (rc<0) return -1;
        else {
            int sent=0;
            if (v->packet->stream_index==v->stream) sent=avcodec_send_packet(v->codec,v->packet);
            av_packet_unref(v->packet); if (sent<0) return -1;
        }
    }
    return -1;
}

/* Short-clip playback is deliberately bounded. Decode audio once, preserving
 * timestamp gaps, then use SDL's consumed-sample count as the video clock. */
#define AUDIO_FRAMES (48000*60)
static int load_audio(MediaVideo *v) {
    MediaVideo *source=media_video_open(v->bytes,v->size);
    AVCodecContext *codec=NULL; SwrContext *swr=NULL;
    int result=-1, draining=0;
    const char *stage="source";
    if (!source) return -1;
    const AVCodec *decoder=NULL;
    int stream=av_find_best_stream(source->format,AVMEDIA_TYPE_AUDIO,-1,-1,&decoder,0);
    if (stream==AVERROR_STREAM_NOT_FOUND) { result=0; goto done; }
    if (stream<0 || !(codec=avcodec_alloc_context3(decoder))) goto done;
    stage="audio codec";
    if (avcodec_parameters_to_context(codec,source->format->streams[stream]->codecpar)<0) goto done;
    codec->thread_count=2;
    if (avcodec_open2(codec,decoder,NULL)<0 || codec->sample_rate<1 || codec->ch_layout.nb_channels<1 || codec->ch_layout.nb_channels>8) goto done;
    AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    if (codec->ch_layout.order==AV_CHANNEL_ORDER_UNSPEC) {
        int channels=codec->ch_layout.nb_channels;
        av_channel_layout_uninit(&codec->ch_layout); av_channel_layout_default(&codec->ch_layout,channels);
    }
    stage="resampler";
    if (swr_alloc_set_opts2(&swr,&stereo,AV_SAMPLE_FMT_S16,48000,&codec->ch_layout,codec->sample_fmt,codec->sample_rate,0,NULL)<0 || swr_init(swr)<0) goto done;
    v->audio=calloc(AUDIO_FRAMES,4); if (!v->audio) goto done;
    size_t written=0;
    for (int budget=0;budget<100000;budget++) {
        stage="decode";
        int rc=avcodec_receive_frame(codec,source->frame);
        if (rc==0) {
            AVFrame *f=source->frame;
            stage="frame format";
            if (f->sample_rate!=codec->sample_rate || f->format!=codec->sample_fmt || av_channel_layout_compare(&f->ch_layout,&codec->ch_layout)) goto done;
            int capacity=swr_get_out_samples(swr,f->nb_samples);
            stage="conversion";
            if (capacity<0 || capacity>AUDIO_FRAMES) goto done;
            unsigned char *temp=malloc((size_t)capacity*4); if (!temp) goto done;
            double stamp=f->best_effort_timestamp==AV_NOPTS_VALUE ? written/48000.0 : f->best_effort_timestamp*av_q2d(source->format->streams[stream]->time_base)-v->origin-swr_get_delay(swr,codec->sample_rate)/(double)codec->sample_rate;
            int count=swr_convert(swr,&temp,capacity,(const uint8_t **)f->extended_data,f->nb_samples);
            if (!isfinite(stamp) || stamp < -60 || stamp > 60 || count<0) { free(temp); goto done; }
            int64_t at=(int64_t)llround(stamp*48000); int skip=at<0 ? (int)fmin(count,-at) : 0;
            if (at<0) at=0;
            if (at+count-skip>AUDIO_FRAMES) { free(temp); goto done; }
            memcpy(v->audio+at*4,temp+skip*4,(size_t)(count-skip)*4); free(temp);
            if ((size_t)(at+count-skip)>written) written=(size_t)(at+count-skip);
            av_frame_unref(f); continue;
        }
        if (rc==AVERROR_EOF) {
            stage="resampler flush";
            for (;;) {
                unsigned char tail[8192]; uint8_t *out=tail;
                int count=swr_convert(swr,&out,2048,NULL,0);
                if (count<0 || written+(size_t)count>AUDIO_FRAMES) goto done;
                if (!count) break;
                memcpy(v->audio+written*4,tail,(size_t)count*4); written+=(size_t)count;
            }
            v->audio_size=written*4; result=0; break;
        }
        if (rc!=AVERROR(EAGAIN) || draining) goto done;
        rc=av_read_frame(source->format,source->packet);
        if (rc==AVERROR_EOF) { draining=1; if (avcodec_send_packet(codec,NULL)<0) goto done; }
        else if (rc<0) goto done;
        else {
            int sent=source->packet->stream_index==stream ? avcodec_send_packet(codec,source->packet) : 0;
            av_packet_unref(source->packet); if (sent<0) goto done;
        }
    }
done:
    if (result<0) SDL_Log("video audio initialization failed: %s",stage);
    swr_free(&swr); avcodec_free_context(&codec); media_video_close(source);
    if (result<0) { free(v->audio); v->audio=NULL; v->audio_size=0; }
    return result;
}
static int player_queue(MediaVideo *v) {
    v->started=SDL_GetTicks64(); v->drained=0; v->queued=0;
    if (v->audio_device) {
        SDL_PauseAudioDevice(v->audio_device,1); SDL_ClearQueuedAudio(v->audio_device);
        size_t offset=(size_t)fmin(v->audio_size,floor(v->position*48000)*4);
        v->queued=(Uint32)(v->audio_size-offset);
        if (v->queued && SDL_QueueAudio(v->audio_device,v->audio+offset,v->queued)) return -1;
        SDL_PauseAudioDevice(v->audio_device,!v->playing);
    }
    return 0;
}
static double player_time(MediaVideo *v) {
    if (!v->playing) return v->position;
    Uint64 now=SDL_GetTicks64();
    if (v->audio_device && v->queued) {
        Uint32 left=SDL_GetQueuedAudioSize(v->audio_device);
        double seconds=v->position+(v->queued-left)/192000.0;
        if (!left) { if (!v->drained) v->drained=now; seconds+=(now-v->drained)/1000.0; }
        return seconds;
    }
    return v->position+(now-v->started)/1000.0;
}
int media_video_play(MediaVideo *v) {
    if (v->playing) return 0;
    if (!v->audio_loaded) {
        if (load_audio(v)<0) return -1;
        v->audio_loaded=1;
    }
    if (v->audio_size && !v->audio_device) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return -1;
        SDL_AudioSpec wanted={0}; wanted.freq=48000; wanted.format=AUDIO_S16SYS; wanted.channels=2; wanted.samples=512;
        v->audio_device=SDL_OpenAudioDevice(NULL,0,&wanted,NULL,0);
        if (!v->audio_device) { SDL_QuitSubSystem(SDL_INIT_AUDIO); return -1; }
    }
    v->playing=1;
    if (player_queue(v)<0) { v->playing=0; return -1; }
    return 0;
}
int media_video_pause(MediaVideo *v) {
    v->position=player_time(v); v->playing=0;
    if (v->audio_device) SDL_PauseAudioDevice(v->audio_device,1);
    return 0;
}
int media_video_tick(MediaVideo *v, SDL_Surface *destination, double *seconds, int *playing) {
    if (!destination || destination->w>2048 || destination->h>2048) return -1;
    if (!v->pending || v->pending->w!=destination->w || v->pending->h!=destination->h) {
        if (v->pending_valid) return -1; /* Resize only between pending frames. */
        SDL_FreeSurface(v->pending);
        v->pending=SDL_CreateRGBSurfaceWithFormat(0,destination->w,destination->h,32,SDL_PIXELFORMAT_ARGB8888);
        if (!v->pending) return -1;
    }
    *seconds=player_time(v);
    for (int budget=0;budget<120;budget++) {
        if (!v->pending_valid && !v->ended) {
            int rc=media_video_next(v,v->pending,&v->pending_seconds);
            if (rc<0) return -1;
            v->pending_valid=rc==1; v->ended=rc==0;
        }
        if (!v->pending_valid || v->pending_seconds>*seconds) break;
        if (SDL_BlitSurface(v->pending,NULL,destination,NULL)) return -1;
        v->pending_valid=0;
    }
    int eof=v->ended && !v->pending_valid && *seconds>=fmax(v->last_seconds+v->frame_seconds,v->audio_size/192000.0);
    if (eof) media_video_pause(v);
    *playing=v->playing;
    return eof;
}
