/*
 * Video mode for QEmacs
 * Copyright (c) 2002, 2003 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "qe.h"
#include "avformat.h"
#include <pthread.h>
#include <math.h>

extern EditBufferDataType video_data_type;

//#define DEBUG

#define MAX_VIDEOQ_SIZE (5 * 256 * 1024)
#define MAX_AUDIOQ_SIZE (5 * 16 * 1024)

#define SAMPLE_ARRAY_SIZE 512

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    int abort_request;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} PacketQueue;

#define VIDEO_PICTURE_QUEUE_SIZE 1

typedef struct VideoPicture {
    int delay; /* delay before showing the next picture */
    QEBitmap *bmp; /* bitmap associated to the picture */
    int width, height; /* source height & width */
    int allocated;
} VideoPicture;

typedef struct VideoState {
    EditState *edit_state;
    pthread_t parse_tid;
    pthread_t audio_tid;
    pthread_t video_tid;
    int no_background;
    int abort_request;
    int paused;
    AVFormatContext *ic;
    
    int audio_stream;
    AVStream *audio_st;
    PacketQueue audioq;
    AVFormatContext *audio_out;
    int16_t sample_array[SAMPLE_ARRAY_SIZE];
    int sample_array_index;
    
    int video_stream;
    AVStream *video_st;
    PacketQueue videoq;

    VideoPicture pictq[VIDEO_PICTURE_QUEUE_SIZE];
    int pictq_size, pictq_rindex, pictq_windex;
    pthread_mutex_t pictq_mutex;
    pthread_cond_t pictq_cond;
    
    QETimer *video_timer;
} VideoState;

static int video_buffer_load(EditBuffer *b, FILE *f)
{
    /* no mode specific data */
    return 0;
}

static int video_buffer_save(EditBuffer *b, const char *filename)
{
    /* cannot save anything */
    return -1;
}

static void video_buffer_close(EditBuffer *b)
{
}

static int video_mode_probe(ModeProbeData *pd)
{
    AVProbeData avpd;
    AVInputFormat *fmt;
    
    avpd.filename = pd->filename;
    avpd.buf = pd->buf;
    avpd.buf_size = pd->buf_size;
    
    fmt = av_probe_input_format(&avpd, 1);
    if (!fmt)
        return 0;
    else
        return 100;
}

/* packet queue handling */
static void packet_queue_init(PacketQueue *q)
{
    memset(q, 0, sizeof(PacketQueue));
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond, NULL);
}

static void packet_queue_end(PacketQueue *q)
{
    AVPacketList *pkt, *pkt1;

    for(pkt = q->first_pkt; pkt != NULL; pkt = pkt1) {
        pkt1 = pkt->next;
        av_free_packet(&pkt->pkt);
    }

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->cond);
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt)
{
    AVPacketList *pkt1;

    pkt1 = av_malloc(sizeof(AVPacketList));
    if (!pkt1)
        return -1;
    pkt1->pkt = *pkt;
    pkt1->next = NULL;

    pthread_mutex_lock(&q->mutex);

    if (!q->last_pkt)

        q->first_pkt = pkt1;
    else
        q->last_pkt->next = pkt1;
    q->last_pkt = pkt1;
    q->nb_packets++;
    q->size += pkt1->pkt.size;

    pthread_cond_signal(&q->cond);

    pthread_mutex_unlock(&q->mutex);
    return 0;
}

static void packet_queue_abort(PacketQueue *q)
{
    pthread_mutex_lock(&q->mutex);

    q->abort_request = 1;
    
    pthread_cond_signal(&q->cond);

    pthread_mutex_unlock(&q->mutex);
}

/* return < 0 if aborted, 0 if no packet and > 0 if packet.  */
static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block)
{
    AVPacketList *pkt1;
    int ret;

    pthread_mutex_lock(&q->mutex);

    for(;;) {
        if (q->abort_request) {
            ret = -1;
            break;
        }
            
        pkt1 = q->first_pkt;
        if (pkt1) {
            q->first_pkt = pkt1->next;
            if (!q->first_pkt)
                q->last_pkt = NULL;
            q->nb_packets--;
            q->size -= pkt1->pkt.size;
            *pkt = pkt1->pkt;
            free(pkt1);
            ret = 1;
            break;
        } else if (!block) {
            ret = 0;
            break;
        } else {
            pthread_cond_wait(&q->cond, &q->mutex);
        }
    }
    pthread_mutex_unlock(&q->mutex);
    return ret;
}

/* called to display each frame */
static void video_refresh_timer(void *opaque)
{
    EditState *s = opaque;
    QEmacsState *qs = &qe_state;
    VideoState *is = s->mode_data;
    VideoPicture *vp;

    if (is->video_st) {
        if (is->pictq_size == 0) {
            /* if no picture, need to wait */
            is->video_timer = qe_add_timer(40, s, video_refresh_timer);
        } else {
            vp = &is->pictq[is->pictq_rindex];
            
            /* launch timer for next picture */
            is->video_timer = qe_add_timer(vp->delay, s, video_refresh_timer);

            /* invalidate window */
            edit_invalidate(s);
            is->no_background = 1; /* XXX: horrible, needs complete rewrite */
            
            /* display picture */
            edit_display(qs);
            dpy_flush(qs->screen);
            
            /* update queue size and signal for next picture */
            if (++is->pictq_rindex == VIDEO_PICTURE_QUEUE_SIZE)
                is->pictq_rindex = 0;
            
            pthread_mutex_lock(&is->pictq_mutex);
            is->pictq_size--;
            pthread_cond_signal(&is->pictq_cond);
            pthread_mutex_unlock(&is->pictq_mutex);
        }
    } else if (is->audio_st) {
        /* draw the next audio frame */
        is->video_timer = qe_add_timer(40, s, video_refresh_timer);

        /* if only audio stream, then display the audio bars (better
           than nothing, just to test the implementation */
        
        /* invalidate window */
        edit_invalidate(s);
        is->no_background = 1; /* XXX: horrible, needs complete rewrite */
        
        /* display picture */
        edit_display(qs);
        dpy_flush(qs->screen);
    } else {
        is->video_timer = qe_add_timer(100, s, video_refresh_timer);
    }
}

static void video_image_display(EditState *s)
{
    VideoState *is = s->mode_data;
    VideoPicture *vp;
    float aspect_ratio;
    int width, height, x, y;

    vp = &is->pictq[is->pictq_rindex];
    if (vp->bmp) {
        /* XXX: use variable in the frame */
        aspect_ratio = is->video_st->codec.aspect_ratio;
        if (aspect_ratio <= 0.0)
            aspect_ratio = (float)is->video_st->codec.width / 
                (float)is->video_st->codec.height;
        /* XXX: we suppose the screen has a 1.0 pixel ratio */
        height = s->height;
        width = ((int)rint(height * aspect_ratio)) & -3;
        if (width > s->width) {
            width = s->width;
            height = ((int)rint(width / aspect_ratio)) & -3;
        }
        x = (s->width - width) / 2;
        y = (s->height - height) / 2;
        if (!is->no_background) {
            /* fill the background */
            fill_border(s, x, y, width, height, QERGB(0x00, 0x00, 0x00));
        } else {
            is->no_background = 0;
        }
        bmp_draw(s->screen, vp->bmp, s->xleft + x, s->ytop + y, 
                 width, height, 0, 0, 0);
    } else {
        fill_rectangle(s->screen, 
                       s->xleft, s->ytop, s->width, s->height, 
                       QERGB(0x00, 0x00, 0x00));
    }
}

static void video_audio_display(EditState *s)
{
    VideoState *is = s->mode_data;
    int i, x, y1, y, h, ys;

    fill_rectangle(s->screen, 
                   s->xleft, s->ytop, s->width, s->height, 
                   QERGB(0x00, 0x00, 0x00));
    if (is->sample_array_index >= SAMPLE_ARRAY_SIZE) {
        i = 0;
        y1 = (s->height >> 1) + s->ytop;
        h = y1;
        for(x = 0; x < s->width; x++) {
            y = (is->sample_array[i] * h) >> 15;
            if (y < 0) {
                y = -y;
                ys = y1 - y;
            } else {
                ys = y1;
            }
            fill_rectangle(s->screen, 
                           s->xleft + x, ys, 1, y, 
                           QERGB(0xff, 0xff, 0xff));
            if (++i >= SAMPLE_ARRAY_SIZE)
                i = 0;
        }

        is->sample_array_index = 0;
    }
}

/* display the current picture, if any */
static void video_display(EditState *s)
{
    VideoState *is = s->mode_data;

    if (s->display_invalid) {
        if (is->video_st)
            video_image_display(s);
        else if (is->audio_st) 
            video_audio_display(s);
        s->display_invalid = 0;
    }
}

/* allocate a picture (needs to do that in main thread to avoid
   potential locking problems */
static void alloc_picture(void *opaque)
{
    VideoState *is = opaque;
    EditState *s = is->edit_state;
    VideoPicture *vp;
    int is_yuv;

    vp = &is->pictq[is->pictq_windex];
    
    if (vp->bmp)
        bmp_free(s->screen, vp->bmp);
    /* XXX: use generic function */
    switch(is->video_st->codec.pix_fmt) {
    case PIX_FMT_YUV420P:
    case PIX_FMT_YUV422P:
    case PIX_FMT_YUV444P:
    case PIX_FMT_YUV422:
    case PIX_FMT_YUV410P:
    case PIX_FMT_YUV411P:
        is_yuv = 1;
        break;
    default:
        is_yuv = 0;
        break;
    }

 retry:
    if (is_yuv) {
        vp->bmp = bmp_alloc(s->screen, 
                            is->video_st->codec.width,
                            is->video_st->codec.height,
                            QEBITMAP_FLAG_VIDEO);
        /* currently we cannot resize, so we fallback to standard if
           no exact size */
        if (vp->bmp->width != is->video_st->codec.width ||
            vp->bmp->height != is->video_st->codec.height) {
            is_yuv = 0;
            if (vp->bmp)
                bmp_free(s->screen, vp->bmp);
            vp->bmp = NULL;
            goto retry;
        }
    } else {
        vp->bmp = bmp_alloc(s->screen, 
                            is->video_st->codec.width,
                            is->video_st->codec.height,
                            0);
    }
    vp->width = is->video_st->codec.width;
    vp->height = is->video_st->codec.height;

    pthread_mutex_lock(&is->pictq_mutex);
    vp->allocated = 1;
    pthread_cond_signal(&is->pictq_cond);
    pthread_mutex_unlock(&is->pictq_mutex);
}

static int output_picture(VideoState *is, AVPicture *src_pict)
{
    EditState *s = is->edit_state;
    VideoPicture *vp;
    QEPicture qepict;
    int dst_pix_fmt, i;
    AVPicture pict;

    /* wait until we have space to put a new picture */
    pthread_mutex_lock(&is->pictq_mutex);
    while (is->pictq_size >= VIDEO_PICTURE_QUEUE_SIZE &&
           !is->videoq.abort_request) {
        pthread_cond_wait(&is->pictq_cond, &is->pictq_mutex);
    }
    pthread_mutex_unlock(&is->pictq_mutex);
    
    if (is->videoq.abort_request)
        return -1;

    vp = &is->pictq[is->pictq_windex];

    /* alloc or resize hardware picture buffer */
    if (!vp->bmp || 
        vp->width != is->video_st->codec.width ||
        vp->height != is->video_st->codec.height) {

        vp->allocated = 0;

        /* the allocation must be done in the main thread to avoid
           locking problems */
        qe_add_timer(0, is, alloc_picture);

        /* wait until the picture is allocated */
        pthread_mutex_lock(&is->pictq_mutex);
        while (!vp->allocated && !is->videoq.abort_request) {
            pthread_cond_wait(&is->pictq_cond, &is->pictq_mutex);
        }
        pthread_mutex_unlock(&is->pictq_mutex);

        if (is->videoq.abort_request)
            return -1;
    }

    if (vp->bmp) {
        /* get a pointer on the bitmap */
        bmp_lock(s->screen, vp->bmp, &qepict, 
                 0, 0, vp->bmp->width, vp->bmp->height);
        dst_pix_fmt = qe_bitmap_format_to_pix_fmt(vp->bmp->format);
        for(i=0;i<4;i++) {
            pict.data[i] = qepict.data[i];
            pict.linesize[i] = qepict.linesize[i];
        }
        img_convert(&pict, dst_pix_fmt, 
                    src_pict, is->video_st->codec.pix_fmt, 
                    is->video_st->codec.width, is->video_st->codec.height);
        /* update the bitmap content */
        bmp_unlock(s->screen, vp->bmp);

        /* compute delay for the next frame */
        vp->delay =  (1000 * is->video_st->codec.frame_rate_base) / 
            is->video_st->codec.frame_rate;
        /* XXX: just fixes .asf! */
        if (vp->delay > 40)
            vp->delay = 40;
        /* now we can update the picture count */
        if (++is->pictq_windex == VIDEO_PICTURE_QUEUE_SIZE)
            is->pictq_windex = 0;
        pthread_mutex_lock(&is->pictq_mutex);
        is->pictq_size++;
        pthread_mutex_unlock(&is->pictq_mutex);
    }
    return 0;
}

static void *video_thread(void *arg)
{
    VideoState *is = arg;
    AVPacket pkt1, *pkt = &pkt1;
    unsigned char *ptr;
    int len, len1, got_picture, i;
    AVFrame frame;
    AVPicture pict;

    for(;;) {
        while (is->paused && !is->videoq.abort_request) {
            usleep(10000);
        }
        if (packet_queue_get(&is->videoq, pkt, 1) < 0)
            break;
        ptr = pkt->data;
        if (is->video_st->codec.codec_id == CODEC_ID_RAWVIDEO) {
            avpicture_fill(&pict, ptr, 
                           is->video_st->codec.pix_fmt,
                           is->video_st->codec.width,
                           is->video_st->codec.height);
            if (output_picture(is, &pict) < 0)
                goto the_end;
        } else {
            len = pkt->size;
            while (len > 0) {
                len1 = avcodec_decode_video(&is->video_st->codec, 
                                            &frame, &got_picture, ptr, len);
                if (len1 < 0)
                    break;
                if (got_picture) {
                    for(i=0;i<4;i++) {
                        pict.data[i] = frame.data[i];
                        pict.linesize[i] = frame.linesize[i];
                    }
                    if (output_picture(is, &pict) < 0)
                        goto the_end;
                }
                ptr += len1;
                len -= len1;
            }
        }
        av_free_packet(pkt);
    }
 the_end:
    return NULL;
}

static void output_audio(VideoState *is, short *samples, int samples_size)
{
    int len, n, channels, i;
    int16_t *out, *in;

    /* copy samples for viewing in editor window */
    len = SAMPLE_ARRAY_SIZE - is->sample_array_index;
    if (len > 0) {
        channels = is->audio_st->codec.channels;
        n = samples_size / (2 * channels);
        if (len > n)
            len = n;
        out = is->sample_array + is->sample_array_index;
        in = samples;
        for(i = 0; i < len; i++) {
            out[i] = in[0];
            in += channels;
        }
        is->sample_array_index += len;
    }

    is->audio_out->oformat->write_packet(is->audio_out, 0, 
                                         (uint8_t *)samples, samples_size, 0);
}

static void *audio_thread(void *arg)
{
    VideoState *is = arg;
    AVPacket pkt1, *pkt = &pkt1;
    unsigned char *ptr;
    int len, len1, data_size;
    short samples[AVCODEC_MAX_AUDIO_FRAME_SIZE / 2];

    for(;;) {
        while (is->paused && !is->audioq.abort_request) {
            usleep(10000);
        }
        if (packet_queue_get(&is->audioq, pkt, 1) < 0)
            break;
        ptr = pkt->data;
        len = pkt->size;
        while (len > 0) {
            len1 = avcodec_decode_audio(&is->audio_st->codec, 
                                        samples, &data_size, ptr, len);
            if (len1 < 0)
                break;
            if (data_size > 0) {
                output_audio(is, samples, data_size);
            }
            ptr += len1;
            len -= len1;
        }
        av_free_packet(pkt);
    }
    return NULL;
}

/* open a given stream. Return 0 if OK */
static int stream_open(EditState *s, int stream_index)
{
    VideoState *is = s->mode_data;
    AVFormatContext *ic = is->ic;
    AVCodecContext *enc;
    AVCodec *codec;
    AVStream *st;

    if (stream_index < 0 || stream_index >= ic->nb_streams)
        return -1;
    enc = &ic->streams[stream_index]->codec;
    

    /* prepare audio output */
    if (enc->codec_type == CODEC_TYPE_AUDIO) {
        is->audio_out = av_mallocz(sizeof(AVFormatContext));
        is->audio_out->oformat = guess_format("audio_device", NULL, NULL);
        st = av_new_stream(is->audio_out, 0);
        st->codec.codec_type = CODEC_TYPE_AUDIO;
        st->codec.codec_id = is->audio_out->oformat->audio_codec;
        st->codec.sample_rate = enc->sample_rate;
        st->codec.channels = enc->channels;
        av_set_parameters(is->audio_out, NULL);
        /* cannot open audio: return error */
        if (av_write_header(is->audio_out) != 0)
            return -1;
    }

    codec = avcodec_find_decoder(enc->codec_id);
    if (!codec ||
        avcodec_open(enc, codec) < 0)
        return -1;
        switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        is->audio_stream = stream_index;
        is->audio_st = ic->streams[stream_index];

        packet_queue_init(&is->audioq);
        pthread_create(&is->audio_tid, NULL, audio_thread, is);
        break;
    case CODEC_TYPE_VIDEO:
        is->video_stream = stream_index;
        is->video_st = ic->streams[stream_index];

        packet_queue_init(&is->videoq);
        pthread_create(&is->video_tid, NULL, video_thread, is);
        break;
    default:
        break;
    }
    return 0;
}

static void stream_close(EditState *s, int stream_index)
{
    VideoState *is = s->mode_data;
    AVFormatContext *ic = is->ic;
    AVCodecContext *enc;
    
    enc = &ic->streams[stream_index]->codec;

    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        packet_queue_abort(&is->audioq);
        pthread_join(is->audio_tid, NULL);

        packet_queue_end(&is->audioq);

        av_write_trailer(is->audio_out);
        av_freep(&is->audio_out);
        
        break;
    case CODEC_TYPE_VIDEO:
        packet_queue_abort(&is->videoq);

        /* note: we also signal this mutex to make sure we deblock the
           video thread in all cases */
        pthread_mutex_lock(&is->pictq_mutex);
        pthread_cond_signal(&is->pictq_cond);
        pthread_mutex_unlock(&is->pictq_mutex);

        pthread_join(is->video_tid, NULL);

        packet_queue_end(&is->videoq);
        break;
    default:
        break;
    }

    avcodec_close(enc);
    switch(enc->codec_type) {
    case CODEC_TYPE_AUDIO:
        is->audio_st = NULL;
        is->audio_stream = -1;
        break;
    case CODEC_TYPE_VIDEO:
        is->video_st = NULL;
        is->video_stream = -1;
        break;
    default:
        break;
    }
}


/* this thread gets the stream from the disk or the network */
static void *decode_thread(void *arg)
{
    EditState *s = arg;
    VideoState *is = s->mode_data;
    AVFormatContext *ic;
    int err, i, ret, video_index, audio_index;
    AVPacket pkt1, *pkt = &pkt1;

    video_index = -1;
    audio_index = -1;
    is->video_stream = -1;
    is->audio_stream = -1;

    err = av_open_input_file(&ic, s->b->filename, NULL, 0, NULL);
    if (err < 0)
        return NULL;
    is->ic = ic;
    err = av_find_stream_info(ic);
    if (err < 0)
        goto fail;

    for(i = 0; i < ic->nb_streams; i++) {
        AVCodecContext *enc = &ic->streams[i]->codec;
        switch(enc->codec_type) {
        case CODEC_TYPE_AUDIO:
            if (audio_index < 0)
                audio_index = i;
            break;
        case CODEC_TYPE_VIDEO:
            if (video_index < 0)
                video_index = i;
            break;
        default:
            break;
        }
    }
#ifdef DEBUG
    dump_format(ic, 0, s->b->filename, 0);
#endif
    
    /* open the streams */
    if (audio_index >= 0) {
        stream_open(s, audio_index);
    }

    if (video_index >= 0) {
        stream_open(s, video_index);
    }

    if (is->video_stream < 0 && is->audio_stream < 0) {
        goto fail;
    }

    for(;;) {
        if (is->abort_request)
            break;
        /* if the queue are full, no need to read more */
        if (is->audioq.size > MAX_AUDIOQ_SIZE ||
            is->videoq.size > MAX_VIDEOQ_SIZE) {
            struct timespec tv;
            /* wait 10 ms */
            tv.tv_sec = 0;
            tv.tv_nsec = 10 * 1000000; 
            nanosleep(&tv, NULL);
            continue;
        }
        ret = av_read_packet(ic, pkt);
        if (ret < 0) {
            break;
        }
        if (pkt->stream_index == is->audio_stream) {
            packet_queue_put(&is->audioq, pkt);
        } else if (pkt->stream_index == is->video_stream) {
            packet_queue_put(&is->videoq, pkt);
        } else {
            av_free_packet(pkt);
        }
    }
    /* wait until the fifo are flushed */
    while (!is->abort_request && (is->audioq.size > 0 || is->videoq.size > 0)) {
        usleep(10000);
    }

 fail:
    /* close each stream */
    if (is->audio_stream >= 0)
        stream_close(s, is->audio_stream);
    if (is->video_stream >= 0)
        stream_close(s, is->video_stream);

    av_close_input_file(is->ic);
    is->ic = NULL; /* safety */
    return NULL;
}

/* pause or resume the video */
static void video_pause(EditState *s)
{
    VideoState *is = s->mode_data;
    is->paused = !is->paused;
}

static int video_mode_init(EditState *s, ModeSavedData *saved_data)
{
    VideoState *is = s->mode_data;
    int err, video_playing;
    EditState *e;

    /* XXX: avoid annoying Overwrite in mode line */
    s->insert = 1;

    /* start video display */
    is->edit_state = s;
    pthread_mutex_init(&is->pictq_mutex, NULL);
    pthread_cond_init(&is->pictq_cond, NULL);

    /* add the refresh timer to draw the picture */
    is->video_timer = qe_add_timer(0, s, video_refresh_timer);

    /* if there is already a window with this video playing, then we
       stop this new instance (C-x 2 case) */
    video_playing = 0;
    for(e = qe_state.first_window; e != NULL; e = e->next_window) {
        if (e->mode == s->mode && e != s && e->b == s->b) {
            VideoState *is1 = e->mode_data;
            if (!is1->paused)
                video_playing = 1;
        }
    }
    if (video_playing) {
        is->paused = 1;
    }
    is->sample_array_index = SAMPLE_ARRAY_SIZE;
    
    err = pthread_create(&is->parse_tid, NULL, decode_thread, s);
    if (err != 0)
        return -1;
    return 0;
}

static void video_mode_close(EditState *s)
{
    VideoState *is = s->mode_data;
    VideoPicture *vp;
    int i;

    /* XXX: use a special url_shutdown call to abort parse cleanly */
    is->abort_request = 1;
    pthread_join(is->parse_tid, NULL);
    /* free all pictures */
    for(i=0;i<VIDEO_PICTURE_QUEUE_SIZE; i++) {
        vp = &is->pictq[i];
        if (vp->bmp) {
            bmp_free(s->screen, vp->bmp);
            vp->bmp = NULL;
        }
    }

    if (is->video_timer) {
        qe_kill_timer(is->video_timer);
    }
}

char *get_stream_id(AVFormatContext *ic, AVStream *st, char *buf, int buf_size)
{
    int flags;
    flags = ic->iformat->flags;
    if (flags & AVFMT_SHOW_IDS) {
        snprintf(buf, buf_size, "%d/0x%x", st->index, st->id);
    } else {
        snprintf(buf, buf_size, "%d", st->index);
    }
    return buf;
}

static void video_mode_line(EditState *s, char *buf, int buf_size)
{
    char *q;
    const char *name;
    VideoState *is = s->mode_data;
    AVCodec *codec;
    AVCodecContext *dec;
    char buf1[32];

    basic_mode_line(s, buf, buf_size, '-');
    q = buf + strlen(buf);
    if (is->paused) {
        q += sprintf(q, "[paused]--");
    }
    if (is->ic) {
        q += sprintf(q, "%s", 
                     is->ic->iformat->name);
    }
    if (is->video_st) {
        name = "???";
        dec = &is->video_st->codec;
        codec = dec->codec;
        if (codec)
            name = codec->name;
        q += sprintf(q, "--%s/%s[%dx%d@%0.2ffps]", 
                     name, get_stream_id(is->ic, is->video_st, buf1, sizeof(buf1)),
                     dec->width, dec->height, 
                     (float)dec->frame_rate / dec->frame_rate_base);
    }
    if (is->audio_st) {
        name = "???";
        dec = &is->audio_st->codec;
        codec = dec->codec;
        if (codec)
            name = codec->name;
        q += sprintf(q, "--%s/%s[%dHz:%dch]", 
                     name, get_stream_id(is->ic, is->audio_st, buf1, sizeof(buf1)),
                     dec->sample_rate, dec->channels);
    }
}

static void av_cycle_stream(EditState *s, int codec_type)
{
    VideoState *is = s->mode_data;
    AVFormatContext *ic = is->ic;
    int start_index, stream_index;
    AVStream *st;
    char buf[32];

    if (codec_type == CODEC_TYPE_VIDEO)
        start_index = is->video_stream;
    else
        start_index = is->audio_stream;
    if (start_index < 0) {
        put_status(s, "No %s stream to cycle", 
                   (codec_type == CODEC_TYPE_VIDEO) ? "video" : "audio");
        return;
    }

    stream_index = start_index;
    for(;;) {
        if (++stream_index >= ic->nb_streams)
            stream_index = 0;
        if (stream_index == start_index) {
            put_status(s, "Only one %s stream", 
                       (codec_type == CODEC_TYPE_VIDEO) ? "video" : "audio");
            return;
        }
        st = ic->streams[stream_index];
        if (st->codec.codec_type == codec_type)
            break;
    }
    put_status(s, "Switching to %s stream %s", 
               (codec_type == CODEC_TYPE_VIDEO) ? "video" : "audio",
               get_stream_id(ic, st, buf, sizeof(buf)));
    stream_close(s, start_index);
    stream_open(s, stream_index);
}

/* specific image commands */
static CmdDef video_commands[] = {
    CMD0( ' ', 'p', "av-pause", video_pause)
    CMD1( 'v', KEY_NONE, "av-cycle-video", av_cycle_stream, CODEC_TYPE_VIDEO)
    CMD1( 'a', KEY_NONE, "av-cycle-audio", av_cycle_stream, CODEC_TYPE_AUDIO)
    CMD_DEF_END,
};

ModeDef video_mode = {
    "av", 
    instance_size: sizeof(VideoState),
    mode_probe: video_mode_probe,
    mode_init: video_mode_init,
    mode_close: video_mode_close,
    display: video_display,
    data_type: &video_data_type,
    mode_line: video_mode_line,
};

static EditBufferDataType video_data_type = {
    "av",
    video_buffer_load,
    video_buffer_save,
    video_buffer_close,
};

int video_init(void)
{
    eb_register_data_type(&video_data_type);
    qe_register_mode(&video_mode);
    qe_register_cmd_table(video_commands, "av");
    /* additionnal mode specific keys */
    qe_register_binding('f', "toggle-full-screen", "av");
    return 0;
}

qe_module_init(video_init);
