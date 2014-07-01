//
// Crude port of av_dump_format into a version that will log through the
// Android logging facilities.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

#include <stdio.h>
#include <stdint.h>
#include <android/log.h>

#include "libavutil/channel_layout.h"
#include "libavutil/display.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/mathematics.h"
#include "libavutil/avstring.h"
#include "libavutil/replaygain.h"
#include "libavformat/avformat.h"


// This is where the magic happens. The rest of the changes were simply
// changing things from using snake_case to CamlCase to avoid symbol
// duplication.
#define avLog(a, b, ...) __android_log_print(ANDROID_LOG_DEBUG, "ffmpegbridge", __VA_ARGS__)


static void printFps(double d, const char *postfix)
{
    uint64_t v = lrintf(d * 100);
    if (v % 100)
        avLog(NULL, AV_LOG_INFO, ", %3.2f %s", d, postfix);
    else if (v % (100 * 1000))
        avLog(NULL, AV_LOG_INFO, ", %1.0f %s", d, postfix);
    else
        avLog(NULL, AV_LOG_INFO, ", %1.0fk %s", d / 1000, postfix);
}

static void dumpMetaData(void *ctx, AVDictionary *m, const char *indent)
{
    if (m && !(av_dict_count(m) == 1 && av_dict_get(m, "language", NULL, 0))) {
        AVDictionaryEntry *tag = NULL;

        avLog(ctx, AV_LOG_INFO, "%sMetadata:\n", indent);
        while ((tag = av_dict_get(m, "", tag, AV_DICT_IGNORE_SUFFIX)))
            if (strcmp("language", tag->key)) {
                const char *p = tag->value;
                avLog(ctx, AV_LOG_INFO,
                       "%s  %-16s: ", indent, tag->key);
                while (*p) {
                    char tmp[256];
                    size_t len = strcspn(p, "\x8\xa\xb\xc\xd");
                    av_strlcpy(tmp, p, FFMIN(sizeof(tmp), len+1));
                    avLog(ctx, AV_LOG_INFO, "%s", tmp);
                    p += len;
                    if (*p == 0xd) avLog(ctx, AV_LOG_INFO, " ");
                    if (*p == 0xa) avLog(ctx, AV_LOG_INFO, "\n%s  %-16s: ", indent, "");
                    if (*p) p++;
                }
                avLog(ctx, AV_LOG_INFO, "\n");
            }
    }
}

/* param change side data*/
static void dumpParamChange(void *ctx, AVPacketSideData *sd)
{
    int size = sd->size;
    const uint8_t *data = sd->data;
    uint32_t flags, channels, sample_rate, width, height;
    uint64_t layout;

    if (!data || sd->size < 4)
        goto fail;

    flags = AV_RL32(data);
    data += 4;
    size -= 4;

    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_COUNT) {
        if (size < 4)
            goto fail;
        channels = AV_RL32(data);
        data += 4;
        size -= 4;
        avLog(ctx, AV_LOG_INFO, "channel count %d, ", channels);
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_CHANNEL_LAYOUT) {
        if (size < 8)
            goto fail;
        layout = AV_RL64(data);
        data += 8;
        size -= 8;
        avLog(ctx, AV_LOG_INFO,
               "channel layout: %s, ", av_get_channel_name(layout));
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_SAMPLE_RATE) {
        if (size < 4)
            goto fail;
        sample_rate = AV_RL32(data);
        data += 4;
        size -= 4;
        avLog(ctx, AV_LOG_INFO, "sample_rate %d, ", sample_rate);
    }
    if (flags & AV_SIDE_DATA_PARAM_CHANGE_DIMENSIONS) {
        if (size < 8)
            goto fail;
        width = AV_RL32(data);
        data += 4;
        size -= 4;
        height = AV_RL32(data);
        data += 4;
        size -= 4;
        avLog(ctx, AV_LOG_INFO, "width %d height %d", width, height);
    }

    return;
fail:
    avLog(ctx, AV_LOG_INFO, "unknown param");
}

/* replaygain side data*/
static void printGain(void *ctx, const char *str, int32_t gain)
{
    avLog(ctx, AV_LOG_INFO, "%s - ", str);
    if (gain == INT32_MIN)
        avLog(ctx, AV_LOG_INFO, "unknown");
    else
        avLog(ctx, AV_LOG_INFO, "%f", gain / 100000.0f);
    avLog(ctx, AV_LOG_INFO, ", ");
}

static void printPeak(void *ctx, const char *str, uint32_t peak)
{
    avLog(ctx, AV_LOG_INFO, "%s - ", str);
    if (!peak)
        avLog(ctx, AV_LOG_INFO, "unknown");
    else
        avLog(ctx, AV_LOG_INFO, "%f", (float) peak / UINT32_MAX);
    avLog(ctx, AV_LOG_INFO, ", ");
}

static void dumpReplayGain(void *ctx, AVPacketSideData *sd)
{
    AVReplayGain *rg;

    if (sd->size < sizeof(*rg)) {
        avLog(ctx, AV_LOG_INFO, "invalid data");
        return;
    }
    rg = (AVReplayGain*)sd->data;

    printGain(ctx, "track gain", rg->track_gain);
    printPeak(ctx, "track peak", rg->track_peak);
    printGain(ctx, "album gain", rg->album_gain);
    printPeak(ctx, "album peak", rg->album_peak);
}

static void dumpSideData(void *ctx, AVStream *st, const char *indent)
{
    int i;

    if (st->nb_side_data)
        avLog(ctx, AV_LOG_INFO, "%sSide data:\n", indent);

    for (i = 0; i < st->nb_side_data; i++) {
        AVPacketSideData sd = st->side_data[i];
        avLog(ctx, AV_LOG_INFO, "%s  ", indent);

        switch (sd.type) {
        case AV_PKT_DATA_PALETTE:
            avLog(ctx, AV_LOG_INFO, "palette");
            break;
        case AV_PKT_DATA_NEW_EXTRADATA:
            avLog(ctx, AV_LOG_INFO, "new extradata");
            break;
        case AV_PKT_DATA_PARAM_CHANGE:
            avLog(ctx, AV_LOG_INFO, "paramchange: ");
            dumpParamChange(ctx, &sd);
            break;
        case AV_PKT_DATA_H263_MB_INFO:
            avLog(ctx, AV_LOG_INFO, "h263 macroblock info");
            break;
        case AV_PKT_DATA_REPLAYGAIN:
            avLog(ctx, AV_LOG_INFO, "replaygain: ");
            dumpReplayGain(ctx, &sd);
            break;
        case AV_PKT_DATA_DISPLAYMATRIX:
            avLog(ctx, AV_LOG_INFO, "displaymatrix: rotation of %.2f degrees",
                   av_display_rotation_get((int32_t *)sd.data));
            break;
        default:
            avLog(ctx, AV_LOG_WARNING,
                   "unknown side data type %d (%d bytes)", sd.type, sd.size);
            break;
        }

        avLog(ctx, AV_LOG_INFO, "\n");
    }
}

/* "user interface" functions */
static void dumpStreamFormat(AVFormatContext *ic, int i,
                               int index, int is_output)
{
    char buf[256];
    int flags = (is_output ? ic->oformat->flags : ic->iformat->flags);
    AVStream *st = ic->streams[i];
    int g = av_gcd(st->time_base.num, st->time_base.den);
    AVDictionaryEntry *lang = av_dict_get(st->metadata, "language", NULL, 0);

    if (!g)
        g = 1;

    avcodec_string(buf, sizeof(buf), st->codec, is_output);
    avLog(NULL, AV_LOG_INFO, "    Stream #%d:%d", index, i);

    /* the pid is an important information, so we display it */
    /* XXX: add a generic system */
    if (flags & AVFMT_SHOW_IDS)
        avLog(NULL, AV_LOG_INFO, "[0x%x]", st->id);
    if (lang)
        avLog(NULL, AV_LOG_INFO, "(%s)", lang->value);
    avLog(NULL, AV_LOG_DEBUG, ", %d, %d/%d", st->codec_info_nb_frames,
           st->time_base.num / g, st->time_base.den / g);
    avLog(NULL, AV_LOG_INFO, ": %s", buf);

    if (st->sample_aspect_ratio.num && // default
        av_cmp_q(st->sample_aspect_ratio, st->codec->sample_aspect_ratio)) {
        AVRational display_aspect_ratio;
        av_reduce(&display_aspect_ratio.num, &display_aspect_ratio.den,
                  st->codec->width  * st->sample_aspect_ratio.num,
                  st->codec->height * st->sample_aspect_ratio.den,
                  1024 * 1024);
        avLog(NULL, AV_LOG_INFO, ", SAR %d:%d DAR %d:%d",
               st->sample_aspect_ratio.num, st->sample_aspect_ratio.den,
               display_aspect_ratio.num, display_aspect_ratio.den);
    }

    if (st->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
        if (st->avg_frame_rate.den && st->avg_frame_rate.num)
            printFps(av_q2d(st->avg_frame_rate), "fps");
#if FF_API_R_FRAME_RATE
        if (st->r_frame_rate.den && st->r_frame_rate.num)
            printFps(av_q2d(st->r_frame_rate), "tbr");
#endif
        if (st->time_base.den && st->time_base.num)
            printFps(1 / av_q2d(st->time_base), "tbn");
        if (st->codec->time_base.den && st->codec->time_base.num)
            printFps(1 / av_q2d(st->codec->time_base), "tbc");
    }

    if (st->disposition & AV_DISPOSITION_DEFAULT)
        avLog(NULL, AV_LOG_INFO, " (default)");
    if (st->disposition & AV_DISPOSITION_DUB)
        avLog(NULL, AV_LOG_INFO, " (dub)");
    if (st->disposition & AV_DISPOSITION_ORIGINAL)
        avLog(NULL, AV_LOG_INFO, " (original)");
    if (st->disposition & AV_DISPOSITION_COMMENT)
        avLog(NULL, AV_LOG_INFO, " (comment)");
    if (st->disposition & AV_DISPOSITION_LYRICS)
        avLog(NULL, AV_LOG_INFO, " (lyrics)");
    if (st->disposition & AV_DISPOSITION_KARAOKE)
        avLog(NULL, AV_LOG_INFO, " (karaoke)");
    if (st->disposition & AV_DISPOSITION_FORCED)
        avLog(NULL, AV_LOG_INFO, " (forced)");
    if (st->disposition & AV_DISPOSITION_HEARING_IMPAIRED)
        avLog(NULL, AV_LOG_INFO, " (hearing impaired)");
    if (st->disposition & AV_DISPOSITION_VISUAL_IMPAIRED)
        avLog(NULL, AV_LOG_INFO, " (visual impaired)");
    if (st->disposition & AV_DISPOSITION_CLEAN_EFFECTS)
        avLog(NULL, AV_LOG_INFO, " (clean effects)");
    avLog(NULL, AV_LOG_INFO, "\n");

    dumpMetaData(NULL, st->metadata, "    ");

    dumpSideData(NULL, st, "    ");
}

void avDumpFormat(AVFormatContext *ic, int index, const char *url, int is_output)
{
    int i;
    uint8_t *printed = ic->nb_streams ? av_mallocz(ic->nb_streams) : NULL;
    if (ic->nb_streams && !printed)
        return;

    avLog(NULL, AV_LOG_INFO, "%s #%d, %s, %s '%s':\n",
           is_output ? "Output" : "Input",
           index,
           is_output ? ic->oformat->name : ic->iformat->name,
           is_output ? "to" : "from", url);
    dumpMetaData(NULL, ic->metadata, "  ");

    if (!is_output) {
        avLog(NULL, AV_LOG_INFO, "  Duration: ");
        if (ic->duration != AV_NOPTS_VALUE) {
            int hours, mins, secs, us;
            int64_t duration = ic->duration + 5000;
            secs  = duration / AV_TIME_BASE;
            us    = duration % AV_TIME_BASE;
            mins  = secs / 60;
            secs %= 60;
            hours = mins / 60;
            mins %= 60;
            avLog(NULL, AV_LOG_INFO, "%02d:%02d:%02d.%02d", hours, mins, secs,
                   (100 * us) / AV_TIME_BASE);
        } else {
            avLog(NULL, AV_LOG_INFO, "N/A");
        }
        if (ic->start_time != AV_NOPTS_VALUE) {
            int secs, us;
            avLog(NULL, AV_LOG_INFO, ", start: ");
            secs = ic->start_time / AV_TIME_BASE;
            us   = abs(ic->start_time % AV_TIME_BASE);
            avLog(NULL, AV_LOG_INFO, "%d.%06d",
                   secs, (int) av_rescale(us, 1000000, AV_TIME_BASE));
        }
        avLog(NULL, AV_LOG_INFO, ", bitrate: ");
        if (ic->bit_rate)
            avLog(NULL, AV_LOG_INFO, "%d kb/s", ic->bit_rate / 1000);
        else
            avLog(NULL, AV_LOG_INFO, "N/A");
        avLog(NULL, AV_LOG_INFO, "\n");
    }

    for (i = 0; i < ic->nb_chapters; i++) {
        AVChapter *ch = ic->chapters[i];
        avLog(NULL, AV_LOG_INFO, "    Chapter #%d.%d: ", index, i);
        avLog(NULL, AV_LOG_INFO,
               "start %f, ", ch->start * av_q2d(ch->time_base));
        avLog(NULL, AV_LOG_INFO,
               "end %f\n", ch->end * av_q2d(ch->time_base));

        dumpMetaData(NULL, ch->metadata, "    ");
    }

    if (ic->nb_programs) {
        int j, k, total = 0;
        for (j = 0; j < ic->nb_programs; j++) {
            AVDictionaryEntry *name = av_dict_get(ic->programs[j]->metadata,
                                                  "name", NULL, 0);
            avLog(NULL, AV_LOG_INFO, "  Program %d %s\n", ic->programs[j]->id,
                   name ? name->value : "");
            dumpMetaData(NULL, ic->programs[j]->metadata, "    ");
            for (k = 0; k < ic->programs[j]->nb_stream_indexes; k++) {
                dumpStreamFormat(ic, ic->programs[j]->stream_index[k],
                                   index, is_output);
                printed[ic->programs[j]->stream_index[k]] = 1;
            }
            total += ic->programs[j]->nb_stream_indexes;
        }
        if (total < ic->nb_streams)
            avLog(NULL, AV_LOG_INFO, "  No Program\n");
    }

    for (i = 0; i < ic->nb_streams; i++)
        if (!printed[i])
            dumpStreamFormat(ic, i, index, is_output);

    av_free(printed);
}
