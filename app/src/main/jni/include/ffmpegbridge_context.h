//
// The main data structure (and helper functions) used by the FFmpeg bridge.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

typedef struct
{
  // context -- must be memory-managed
  char *output_fmt_name;
  char *output_url;
  AVFormatContext *output_fmt_ctx;
  AVRational *device_time_base;

  // for convenience
  int video_stream_index;
  AVStream *video_stream;
  AVCodec *video_codec;
  int audio_stream_index;
  AVStream *audio_stream;
  AVCodec *audio_codec;

  // video config
  enum AVCodecID video_codec_id;
  enum AVPixelFormat video_pix_fmt;
  int video_width;
  int video_height;
  int video_fps;
  int video_bit_rate;

  // audio config
  enum AVCodecID audio_codec_id;
  enum AVSampleFormat audio_sample_fmt;
  int audio_sample_rate;
  int audio_num_channels;
  int audio_bit_rate;
} FFmpegBridgeContext;


FFmpegBridgeContext* ffmpbr_init(
  const char* output_format_name,
  const char* output_url,
  int video_width,
  int video_height,
  int video_fps,
  int video_bit_rate,
  int audio_sample_rate,
  int audio_num_channels,
  int audio_bit_rate);

void ffmpbr_set_audio_codec_extradata(FFmpegBridgeContext *br_ctx, const int8_t *codec_extradata,
  int codec_extradata_size);
void ffmpbr_set_video_codec_extradata(FFmpegBridgeContext *br_ctx, const int8_t *codec_extradata,
  int codec_extradata_size);

void ffmpbr_write_header(FFmpegBridgeContext *br_ctx);

void ffmpbr_write_packet(FFmpegBridgeContext *br_ctx, uint8_t *data, int data_size, long pts,
    int is_video, int is_video_keyframe);

void ffmpbr_finalize(FFmpegBridgeContext *br_ctx);
