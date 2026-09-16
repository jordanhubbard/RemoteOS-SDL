/* Bounded, in-memory Matroska export: MPEG-4 video and optional stereo PCM. */
#include "media.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <stdlib.h>
#include <string.h>
#define EXPORT_LIMIT (16*1024*1024)
struct MediaEncoder {
    AVFormatContext *format;
    AVCodecContext *codec;
    AVStream *video, *audio;
    AVIOContext *io;
    AVFrame *frame;
    AVPacket *packet;
    struct SwsContext *scale;
    unsigned char *bytes;
    size_t size, offset;
    int fps, frames, finished, failed;
};
static int output_write(void *opaque, const uint8_t *bytes, int length) {
    MediaEncoder *e=opaque;
    if (length<0 || (size_t)length>EXPORT_LIMIT-e->offset) { e->failed=1; return AVERROR(ENOSPC); }
    memcpy(e->bytes+e->offset,bytes,(size_t)length); e->offset+=(size_t)length;
    if (e->offset>e->size) e->size=e->offset;
    return length;
}
/* FFmpeg < 7 uses a non-const write callback. */
#if LIBAVFORMAT_VERSION_MAJOR < 61
static int output_write_legacy(void *opaque, uint8_t *bytes, int length) {
    return output_write(opaque,bytes,length);
}
#endif
static int64_t output_seek(void *opaque, int64_t offset, int whence) {
    MediaEncoder *e=opaque;
    if (whence==AVSEEK_SIZE) return (int64_t)e->size;
    whence &= ~AVSEEK_FORCE;
    int64_t base=whence==SEEK_SET ? 0 : whence==SEEK_CUR ? (int64_t)e->offset : whence==SEEK_END ? (int64_t)e->size : -1;
    if (base<0 || offset < -base || offset > EXPORT_LIMIT-base) return AVERROR(EINVAL);
    e->offset=(size_t)(base+offset); return (int64_t)e->offset;
}
void media_encoder_close(MediaEncoder *e) {
    if (!e) return;
    sws_freeContext(e->scale); av_frame_free(&e->frame); av_packet_free(&e->packet);
    avcodec_free_context(&e->codec);
    if (e->format) e->format->pb=NULL;
    avformat_free_context(e->format);
    if (e->io) { av_freep(&e->io->buffer); avio_context_free(&e->io); }
    free(e->bytes); free(e);
}
MediaEncoder *media_encoder_open(int width, int height, int fps, int audio) {
    if (width<2 || height<2 || width>2048 || height>2048 || width%2 || height%2 || fps<1 || fps>60 || 48000%fps) return NULL;
    MediaEncoder *e=calloc(1,sizeof(*e)); if (!e) return NULL;
    e->fps=fps; e->bytes=calloc(1,EXPORT_LIMIT);
    if (!e->bytes || avformat_alloc_output_context2(&e->format,NULL,"matroska",NULL)<0) goto fail;
    const AVCodec *codec=avcodec_find_encoder(AV_CODEC_ID_MPEG4);
    if (!codec || !(e->codec=avcodec_alloc_context3(codec))) goto fail;
    e->codec->width=width; e->codec->height=height; e->codec->pix_fmt=AV_PIX_FMT_YUV420P;
    e->codec->time_base=(AVRational){1,fps}; e->codec->framerate=(AVRational){fps,1};
    e->codec->bit_rate=4000000; e->codec->gop_size=fps; e->codec->max_b_frames=0;
    e->codec->thread_count=2;
    if (e->format->oformat->flags & AVFMT_GLOBALHEADER) e->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(e->codec,codec,NULL)<0) goto fail;
    e->video=avformat_new_stream(e->format,NULL);
    if (!e->video || avcodec_parameters_from_context(e->video->codecpar,e->codec)<0) goto fail;
    e->video->time_base=e->codec->time_base; e->video->avg_frame_rate=e->codec->framerate;
    if (audio) {
        e->audio=avformat_new_stream(e->format,NULL); if (!e->audio) goto fail;
        AVCodecParameters *p=e->audio->codecpar;
        p->codec_type=AVMEDIA_TYPE_AUDIO; p->codec_id=AV_CODEC_ID_PCM_S16LE;
        p->sample_rate=48000; p->format=AV_SAMPLE_FMT_S16; p->bits_per_coded_sample=16;
        p->block_align=4; p->bit_rate=48000*32; av_channel_layout_default(&p->ch_layout,2);
        e->audio->time_base=(AVRational){1,48000};
    }
    unsigned char *buffer=av_malloc(32768); if (!buffer) goto fail;
#if LIBAVFORMAT_VERSION_MAJOR < 61
    e->io=avio_alloc_context(buffer,32768,1,e,NULL,output_write_legacy,output_seek);
#else
    e->io=avio_alloc_context(buffer,32768,1,e,NULL,output_write,output_seek);
#endif
    if (!e->io) { av_free(buffer); goto fail; }
    e->format->pb=e->io; e->format->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_write_header(e->format,NULL)<0) goto fail;
    e->frame=av_frame_alloc(); e->packet=av_packet_alloc();
    if (!e->frame || !e->packet) goto fail;
    e->frame->format=e->codec->pix_fmt; e->frame->width=width; e->frame->height=height;
    if (av_frame_get_buffer(e->frame,32)<0) goto fail;
    e->scale=sws_getContext(width,height,AV_PIX_FMT_BGRA,width,height,AV_PIX_FMT_YUV420P,SWS_BILINEAR,NULL,NULL,NULL);
    if (!e->scale) goto fail;
    return e;
fail: media_encoder_close(e); return NULL;
}
static int write_video(MediaEncoder *e, AVFrame *frame) {
    if (avcodec_send_frame(e->codec,frame)<0) return -1;
    for (;;) {
        int rc=avcodec_receive_packet(e->codec,e->packet);
        if (rc==AVERROR(EAGAIN) || rc==AVERROR_EOF) return 0;
        if (rc<0) return -1;
        e->packet->duration=1;
        av_packet_rescale_ts(e->packet,e->codec->time_base,e->video->time_base);
        e->packet->stream_index=e->video->index;
        rc=av_interleaved_write_frame(e->format,e->packet); av_packet_unref(e->packet);
        if (rc<0 || e->failed) return -1;
    }
}
int media_encoder_frame(MediaEncoder *e, SDL_Surface *surface, const void *pcm, size_t size) {
    if (!e || e->finished || e->failed || e->frames>=e->fps*60 || !surface ||
        surface->w!=e->codec->width || surface->h!=e->codec->height ||
        size!=(e->audio ? (size_t)(48000/e->fps)*4 : 0)) return -1;
    SDL_Surface *converted=SDL_ConvertSurfaceFormat(surface,SDL_PIXELFORMAT_BGRA32,0);
    if (!converted) return -1;
    int rc=av_frame_make_writable(e->frame);
    if (rc>=0) {
        const uint8_t *src[4]={converted->pixels,NULL,NULL,NULL}; int pitches[4]={converted->pitch,0,0,0};
        rc=sws_scale(e->scale,src,pitches,0,converted->h,e->frame->data,e->frame->linesize);
    }
    SDL_FreeSurface(converted);
    if (rc<0) { e->failed=1; return -1; }
    e->frame->pts=e->frames;
    if (write_video(e,e->frame)<0) { e->failed=1; return -1; }
    if (e->audio) {
        if (av_new_packet(e->packet,(int)size)<0) { e->failed=1; return -1; }
        memcpy(e->packet->data,pcm,size);
        e->packet->pts=e->packet->dts=(int64_t)e->frames*(48000/e->fps);
        e->packet->duration=48000/e->fps; e->packet->stream_index=e->audio->index;
        av_packet_rescale_ts(e->packet,(AVRational){1,48000},e->audio->time_base);
        rc=av_interleaved_write_frame(e->format,e->packet); av_packet_unref(e->packet);
        if (rc<0) { e->failed=1; return -1; }
    }
    avio_flush(e->io); e->frames++;
    if (e->io->error<0 || e->failed) { e->failed=1; return -1; }
    return 0;
}
int media_encoder_finish(MediaEncoder *e) {
    if (!e || e->failed || !e->frames) return -1;
    if (e->finished) return 0;
    if (write_video(e,NULL)<0 || av_write_trailer(e->format)<0) { e->failed=1; return -1; }
    avio_flush(e->io);
    if (e->io->error<0 || e->failed) { e->failed=1; return -1; }
    e->finished=1; return 0;
}
const unsigned char *media_encoder_data(MediaEncoder *e, size_t *size) {
    if (!e || !e->finished || e->failed) return NULL;
    *size=e->size; return e->bytes;
}
