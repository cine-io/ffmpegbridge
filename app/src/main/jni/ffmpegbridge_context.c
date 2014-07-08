//
// Helper functions for the main data structure used by the FFmpeg bridge.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

#include <string.h>

#include "ffmpegbridge_context.h"
#include "ffmpegbridge_log.h"
#include "logdump.h"

//
//-- helper functions
//

void _init_ffmpeg() {
  // initialize FFmpeg
  av_register_all();
  avformat_network_init();
  avcodec_register_all();
}

void _init_device_time_base(FFmpegBridgeContext *br_ctx) {
  // timestamps from the device should be in microseconds
  br_ctx->device_time_base = av_malloc(sizeof(AVRational));
  br_ctx->device_time_base->num = 1;
  br_ctx->device_time_base->den = 1000000;
}

void _init_output_fmt_context(FFmpegBridgeContext *br_ctx) {
  int rc;
  AVOutputFormat *fmt;

  LOGI("_init_output_fmt_context format: %s path: %s", br_ctx->output_fmt_name, br_ctx->output_url);
  rc = avformat_alloc_output_context2(&br_ctx->output_fmt_ctx, NULL, br_ctx->output_fmt_name, br_ctx->output_url);
  if (rc < 0) {
    LOGE("Error getting format context for output path: %s", av_err2str(rc));
  }

  fmt = br_ctx->output_fmt_ctx->oformat;
  fmt->video_codec = br_ctx->video_codec_id;
  fmt->audio_codec = br_ctx->audio_codec_id;

  LOGD("fmt->name: %s", fmt->name);
  LOGD("fmt->long_name: %s", fmt->long_name);
  LOGD("fmt->mime_type: %s", fmt->mime_type);
  LOGD("fmt->extensions: %s", fmt->extensions);
  LOGD("fmt->audio_codec: %d", fmt->audio_codec);
  LOGD("fmt->video_codec: %d", fmt->video_codec);
  LOGD("fmt->subtitle_codec: %d", fmt->subtitle_codec);
  LOGD("fmt->flags: %d", fmt->flags);
}

AVStream* _add_stream(FFmpegBridgeContext *br_ctx, enum AVCodecID codec_id) {
  AVStream *st;
  AVCodec *codec;
  AVCodecContext *c;

  codec = avcodec_find_decoder(codec_id);
  if (!codec) {
    LOGE("ERROR: _add_stream -- codec %d not found", codec_id);
  }
  LOGD("codec->name: %s", codec->name);
  LOGD("codec->long_name: %s", codec->long_name);
  LOGD("codec->type: %d", codec->type);
  LOGD("codec->id: %d", codec->id);
  LOGD("codec->capabilities: %d", codec->capabilities);

  st = avformat_new_stream(br_ctx->output_fmt_ctx, codec);
  if (!st) {
    LOGE("ERROR: _add_stream -- could not allocate new stream");
    return NULL;
  }
  // TODO: need avcodec_get_context_defaults3?
  //avcodec_get_context_defaults3(st->codec, codec);
  st->id = br_ctx->output_fmt_ctx->nb_streams-1;
  c = st->codec;
  LOGI("_add_stream at index %d", st->index);

  // TODO: need this?
  // Some formats want stream headers to be separate.
  if (br_ctx->output_fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
    LOGD("_add_stream: using separate headers");
    c->flags |= CODEC_FLAG_GLOBAL_HEADER;
  }

  LOGD("_add_stream st: %p", st);
  return st;
}

void _add_video_stream(FFmpegBridgeContext *br_ctx) {
  AVCodecContext *c;

  br_ctx->video_stream = _add_stream(br_ctx, br_ctx->video_codec_id);
  br_ctx->video_stream_index = br_ctx->video_stream->index;
  c = br_ctx->video_stream->codec;

  // video parameters
  c->codec_id = br_ctx->video_codec_id;
  c->pix_fmt = br_ctx->video_pix_fmt;
  c->width = br_ctx->video_width;
  c->height = br_ctx->video_height;
  c->bit_rate = br_ctx->video_bit_rate;

  // timebase: This is the fundamental unit of time (in seconds) in terms
  // of which frame timestamps are represented. For fixed-fps content,
  // timebase should be 1/framerate and timestamp increments should be
  // identical to 1.
  c->time_base.den = br_ctx->video_fps;
  c->time_base.num = 1;

  // TODO: need this?
  //c->gop_size = 12; // emit one intra frame every twelve frames at most
}

void _add_audio_stream(FFmpegBridgeContext *br_ctx) {
  AVCodecContext *c;

  br_ctx->audio_stream = _add_stream(br_ctx, br_ctx->audio_codec_id);
  br_ctx->audio_stream_index = br_ctx->audio_stream->index;
  c = br_ctx->audio_stream->codec;

  // audio parameters
  // TODO: keep this FF_COMPLIANCE_UNOFFICIAL?
  c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL; // for native aac support
  c->sample_fmt  = br_ctx->audio_sample_fmt;
  c->sample_rate = br_ctx->audio_sample_rate;
  c->channels    = br_ctx->audio_num_channels;
  c->bit_rate    = br_ctx->audio_bit_rate;
  //c->time_base.num = 1;
  //c->time_base.den = c->sample_rate;
}

// open a file for writing
int _open_output_url(FFmpegBridgeContext *br_ctx){
  int rc;
  if (!(br_ctx->output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    LOGI("Opening output file for writing at path %s", br_ctx->output_url);
    return avio_open(&br_ctx->output_fmt_ctx->pb, br_ctx->output_url, AVIO_FLAG_WRITE);
  } else {
    LOGD("This format does not require a file.");
    return 0;
  }
}

void _write_header(FFmpegBridgeContext *br_ctx){
  LOGI("Writing header ...");
  int rc = avformat_write_header(br_ctx->output_fmt_ctx, NULL);
  if (rc < 0) {
    LOGE("Error writing header: %s", av_err2str(rc));
  }
}

uint8_t* _filter_packet(FFmpegBridgeContext *br_ctx, AVStream *st, AVPacket *packet) {
  int rc = 0;
  uint8_t *filtered_data;
  int filtered_data_size = 0;

  if (st->codec->codec_id == br_ctx->audio_codec_id) {
    LOGD("About to filter audio packet buffer ...");
    filtered_data = av_malloc(packet->size);
    AVBitStreamFilterContext* bsfc = av_bitstream_filter_init("aac_adtstoasc");
    if (!bsfc) {
      LOGE("Error creating aac_adtstoasc bitstream filter.");
    }
    rc = av_bitstream_filter_filter(bsfc, st->codec, NULL,
      &filtered_data, &filtered_data_size,
      packet->data, packet->size,
      packet->flags & AV_PKT_FLAG_KEY);
    if (rc < 0) {
      LOGE("ERROR: Failed to filter bitstream -- %s", av_err2str(rc));
    }

    // don't free packet->data, as it's owned by the JVM
    packet->data = filtered_data;
    packet->size = filtered_data_size;

    return filtered_data;
  } else {
    return 0;
  }
}

void _rescale_packet(FFmpegBridgeContext *br_ctx, AVStream *st, AVPacket *packet) {
  LOGD("time bases: stream=%d/%d, codec=%d/%d, device=%d/%d",
    st->time_base.num, st->time_base.den,
    st->codec->time_base.num, st->codec->time_base.den,
    (*br_ctx->device_time_base).num, (*br_ctx->device_time_base).den);

  packet->pts = av_rescale_q(packet->pts, *(br_ctx->device_time_base), st->time_base);
  packet->dts = av_rescale_q(packet->dts, *(br_ctx->device_time_base), st->time_base);
  // COPIED: from ffmpeg remuxing.c
  //packet->pts = av_rescale_q_rnd(packet->pts, *(br_ctx->device_time_base), st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
  //packet->dts = av_rescale_q_rnd(packet->dts, *(br_ctx->device_time_base), st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
  //packet->duration = av_rescale_q(packet->duration, *(br_ctx->device_time_base), st->time_base);
  //packet->pos = -1;
}

void _write_packet(FFmpegBridgeContext *br_ctx, AVPacket *packet) {
  int rc;

  LOGD("writing frame to stream %d: (pts=%lld, size=%d)",
    packet->stream_index, packet->pts, packet->size);

  rc = av_interleaved_write_frame(br_ctx->output_fmt_ctx, packet);
  if (rc < 0){
    LOGE("ERROR: _write_packet stream (stream %d) -- %s",
      packet->stream_index, av_err2str(rc));
  }
}

void _write_trailer(FFmpegBridgeContext *br_ctx){
  LOGI("Writing trailer ...");
  int rc = av_write_trailer(br_ctx->output_fmt_ctx);
  if (rc < 0) {
    LOGE("Error writing trailer: %s", av_err2str(rc));
  }
}


//
//-- FFmpegBridgeContext API
//

FFmpegBridgeContext* ffmpbr_init(
  const char* output_fmt_name,
  int video_width,
  int video_height,
  int video_fps,
  int video_bit_rate,
  int audio_sample_rate,
  int audio_num_channels,
  int audio_bit_rate) {

  // allocate the memory
  FFmpegBridgeContext *br_ctx = av_malloc(sizeof(FFmpegBridgeContext));

  // defaults -- likely not overridden
  br_ctx->video_codec_id = CODEC_ID_H264;
  br_ctx->video_pix_fmt = PIX_FMT_YUV420P;
  br_ctx->audio_codec_id = CODEC_ID_AAC;
  br_ctx->audio_sample_fmt = AV_SAMPLE_FMT_S16;

  // propagate the configuration
  br_ctx->output_fmt_name = av_strdup(output_fmt_name);
  br_ctx->video_width = video_width;
  br_ctx->video_height = video_height;
  br_ctx->video_fps = video_fps;
  br_ctx->video_bit_rate = video_bit_rate;
  br_ctx->audio_sample_rate = audio_sample_rate;
  br_ctx->audio_num_channels = audio_num_channels;
  br_ctx->audio_bit_rate = audio_bit_rate;

  // initialize FFmpeg
  _init_ffmpeg();

  // initialize our device time_base
  _init_device_time_base(br_ctx);

  return br_ctx;
}

void ffmpbr_prepare_stream(FFmpegBridgeContext *br_ctx, const char *output_url) {
  int rc;

  LOGD("duplicating output_url ...");
  LOGD("br_ctx: %p ...", br_ctx);
  br_ctx->output_url = av_strdup(output_url);

  // initialize our output format context
  LOGD("initializing output_fmt_context ...");
  _init_output_fmt_context(br_ctx);

  // set up the streams
  LOGD("adding video stream ...");
  _add_video_stream(br_ctx);
  LOGD("adding audio stream ...");
  _add_audio_stream(br_ctx);

  LOGD("opening output url ...");
  rc = _open_output_url(br_ctx);
  if (rc < 0){
    LOGE("ERROR: ffmpbr_prepare_stream error -- %s", av_err2str(rc));
  }

  LOGD("logging (dumping) output_fmt_ctx log ...");
  avDumpFormat(br_ctx->output_fmt_ctx, 0, output_url, 1);
}

void ffmpbr_write_header(FFmpegBridgeContext *br_ctx, const int8_t *video_codec_extradata,
  int video_codec_extradata_size) {

  br_ctx->video_stream->codec->extradata = av_malloc(video_codec_extradata_size);
  br_ctx->video_stream->codec->extradata_size = video_codec_extradata_size;
  memcpy(br_ctx->video_stream->codec->extradata, video_codec_extradata, video_codec_extradata_size);

  _write_header(br_ctx);
}

void ffmpbr_write_packet(FFmpegBridgeContext *br_ctx, uint8_t *data, int data_size, long pts,
    int is_video) {
  AVPacket *packet;
  AVStream *st;
  AVCodecContext *c;
  uint8_t *filtered_data = NULL;

  packet = av_malloc(sizeof(AVPacket));
  if (!packet) {
    LOGE("ERROR: ffmpbr_write_packet couldn't allocate memory for the AVPacket");
  }
  av_init_packet(packet);

  if (is_video) {
    packet->stream_index = br_ctx->video_stream_index;
  } else {
    packet->stream_index = br_ctx->audio_stream_index;
  }
  packet->size = data_size;
  packet->pts = packet->dts = pts;
  packet->data = data;
  st = br_ctx->output_fmt_ctx->streams[packet->stream_index];
  c = st->codec;

  // filter the packet (if necessary)
  filtered_data = _filter_packet(br_ctx, st, packet);

  // rescale the timing information for the packet
  _rescale_packet(br_ctx, st, packet);

  // write the frame
  _write_packet(br_ctx, packet);

  // clean up
  if (filtered_data) {
    av_free(filtered_data);
  }
  av_free_packet(packet);
}

void ffmpbr_finalize(FFmpegBridgeContext *br_ctx) {
  // write the file trailer
  LOGD("br_ctx: %p", br_ctx);
  LOGD("calling _write_trailer ...");
  _write_trailer(br_ctx);

  // close the output file
  LOGD("checking for AVFMT_NOFILE ...");
  if (!(br_ctx->output_fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
    LOGD("closing file / stream ...");
    avio_close(br_ctx->output_fmt_ctx->pb);
  }

  // clean up memory
  LOGD("cleaning up device_time_base ...");
  if (br_ctx->device_time_base) av_free(br_ctx->device_time_base);
  LOGD("cleaning up output_fmt_name ...");
  if (br_ctx->output_fmt_name) av_free(br_ctx->output_fmt_name);
  LOGD("cleaning up output_url ...");
  if (br_ctx->output_url) av_free(br_ctx->output_url);
  //LOGD("cleaning up video_stream->codec->extradata ...");
  //LOGD("br_ctx->video_stream: %p ...", br_ctx->video_stream);
  //LOGD("br_ctx->video_stream->codec: %p ...", br_ctx->video_stream->codec);
  //LOGD("br_ctx->video_stream->codec->extradata: %p ...", br_ctx->video_stream->codec->extradata);
  //if (br_ctx->video_stream->codec->extradata) av_free(br_ctx->video_stream->codec->extradata);
  LOGD("cleaning up output_fmt_ctx: %p ...", br_ctx->output_fmt_ctx);
  if (br_ctx->output_fmt_ctx) avformat_free_context(br_ctx->output_fmt_ctx);
  LOGD("cleaning up br_ctx: %p ...", br_ctx);
  av_free(br_ctx);
}
