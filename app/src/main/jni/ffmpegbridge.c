//
// JNI FFmpeg bridge for muxing H.264 and AAC streams into an FLV container
// for streaming over RTMP from an Android device.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

#include <jni.h>
#include "ffmpegbridge.h"
#include "ffmpegbridge_log.h"
#include "ffmpegbridge_context.h"


// our context object
FFmpegBridgeContext* br_ctx;


//
// JNI interface
//

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_init
(JNIEnv *env, jobject jThis, jobject jOpts) {

  const char *output_fmt_name, *output_url;
  int video_width, video_height, video_fps, video_bit_rate;
  int audio_sample_rate, audio_num_channels, audio_bit_rate;

  LOGD("init");

  // get the java object fields
  jclass ClassAVOptions = (*env)->GetObjectClass(env, jOpts);

  jfieldID jOutputFormatName = (*env)->GetFieldID(env, ClassAVOptions, "outputFormatName", "Ljava/lang/String;");
  jfieldID jOutputUrl = (*env)->GetFieldID(env, ClassAVOptions, "outputUrl", "Ljava/lang/String;");

  jfieldID jVideoHeightId = (*env)->GetFieldID(env, ClassAVOptions, "videoHeight", "I");
  jfieldID jVideoWidthId = (*env)->GetFieldID(env, ClassAVOptions, "videoWidth", "I");
  jfieldID jVideoFpsId = (*env)->GetFieldID(env, ClassAVOptions, "videoFps", "I");
  jfieldID jVideoBitRateId = (*env)->GetFieldID(env, ClassAVOptions, "videoBitRate", "I");

  jfieldID jAudioSampleRateId = (*env)->GetFieldID(env, ClassAVOptions, "audioSampleRate", "I");
  jfieldID jAudioNumChannelsId = (*env)->GetFieldID(env, ClassAVOptions, "audioNumChannels", "I");
  jfieldID jAudioBitRateId = (*env)->GetFieldID(env, ClassAVOptions, "audioBitRate", "I");

  // read the java object fields and initialize our variables from above
  jstring outputFormatNameString = (jstring) (*env)->GetObjectField(env, jOpts, jOutputFormatName);
  output_fmt_name = (*env)->GetStringUTFChars(env, outputFormatNameString, NULL);
  jstring outputUrlString = (jstring) (*env)->GetObjectField(env, jOpts, jOutputUrl);
  output_url = (*env)->GetStringUTFChars(env, outputUrlString, NULL);

  video_height = (*env)->GetIntField(env, jOpts, jVideoHeightId);
  video_width = (*env)->GetIntField(env, jOpts, jVideoWidthId);
  video_fps = (*env)->GetIntField(env, jOpts, jVideoFpsId);
  video_bit_rate = (*env)->GetIntField(env, jOpts, jVideoBitRateId);

  audio_sample_rate = (*env)->GetIntField(env, jOpts, jAudioSampleRateId);
  audio_num_channels = (*env)->GetIntField(env, jOpts, jAudioNumChannelsId);
  audio_bit_rate = (*env)->GetIntField(env, jOpts, jAudioBitRateId);

  // initialize our context
  br_ctx = ffmpbr_init(output_fmt_name, output_url,
    video_width, video_height, video_fps, video_bit_rate,
    audio_sample_rate, audio_num_channels, audio_bit_rate);

  (*env)->ReleaseStringUTFChars(env, outputFormatNameString, output_fmt_name);
  (*env)->ReleaseStringUTFChars(env, outputUrlString, output_url);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_setAudioCodecExtraData
(JNIEnv *env, jobject self, jbyteArray jData, jint jSize) {

  LOGD("setAudioCodecExtraData");

  jbyte* raw_bytes = (*env)->GetByteArrayElements(env, jData, NULL);

  // add the extra data to the video codec and write-out the header
  ffmpbr_set_audio_codec_extradata(br_ctx, (int8_t *)raw_bytes, (int)jSize);

  (*env)->ReleaseByteArrayElements(env, jData, raw_bytes, 0);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_setVideoCodecExtraData
(JNIEnv *env, jobject self, jbyteArray jData, jint jSize) {

  LOGD("setVideoCodecExtraData");

  jbyte *raw_bytes = (*env)->GetByteArrayElements(env, jData, NULL);

  // add the extra data to the video codec and write-out the header
  ffmpbr_set_video_codec_extradata(br_ctx, (int8_t *)raw_bytes, (int)jSize);

  (*env)->ReleaseByteArrayElements(env, jData, raw_bytes, 0);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_writeHeader
  (JNIEnv *env, jobject self) {

  LOGD("writeHeader");

  ffmpbr_write_header(br_ctx);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_writePacket
(JNIEnv *env, jobject self, jobject jData, jint jSize, jlong jPts,
 jint jIsVideo, jint jIsVideoKeyframe) {

  uint8_t *data = (*env)->GetDirectBufferAddress(env, jData);
  int is_video = (((int)jIsVideo) == JNI_TRUE);
  int is_video_keyframe = (((int)jIsVideoKeyframe) == JNI_TRUE);

  // write the packet
  ffmpbr_write_packet(br_ctx, data, (int)jSize, (long)jPts, is_video, is_video_keyframe);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_finalize
(JNIEnv *env, jobject self) {

  LOGD("finalize");

  // write out the trailer and clean up
  ffmpbr_finalize(br_ctx);
}
