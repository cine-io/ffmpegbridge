//
// JNI FFmpeg bridge for muxing H.264 and AAC streams into an FLV container
// for streaming over RTMP from an Android device.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

#include <jni.h>
#include <android/log.h>
#include "ffmpegbridge.h"
#include "logdump.h"

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"

// android logging helpers
#define LOG_TAG "ffmpegbridge"
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)


//
//-- variables
//

// output
const char *outputPath;
const char *outputFormatName;
int audioStreamIndex = -1;
int videoStreamIndex = -1;

// A/V
int VIDEO_CODEC_ID = CODEC_ID_H264;
int VIDEO_PIX_FMT = PIX_FMT_YUV420P;
int videoWidth = 1280;
int videoHeight = 720;
int videoFps = 30;
int videoBitRate = 1500000;

int AUDIO_CODEC_ID = CODEC_ID_AAC;
int AUDIO_SAMPLE_FMT = AV_SAMPLE_FMT_S16;
//int AUDIO_SAMPLE_FMT = AV_SAMPLE_FMT_FLTP;
int audioSampleRate = 44100;
int audioNumChannels = 1;
int audioBitRate = 128000;


AVFormatContext *outputFormatContext;
AVStream *audioStream;
AVStream *videoStream;
AVCodec *audioCodec;
AVCodec *videoCodec;
AVRational *deviceTimeBase;

// recycled across calls to writeAVPacketFromEncodedData
AVPacket *packet;

// debugging
int videoFrameCount = 0;


//
//-- helper functions
//

// must be called before we attempt to use the FFmpeg libraries
void init(){
    av_register_all();
    avformat_network_init();
    avcodec_register_all();
}

// add a stream
AVStream* addStream(AVFormatContext *dest, enum AVCodecID codecId, AVCodec *codec) {
	AVCodecContext *c;
	AVStream *st;

    if (!codec) {
        codec = avcodec_find_decoder(codecId);
        if (!codec) {
            LOGE("ERROR: addStream codec not found");
        }
	}
	LOGD("codec->name: %s", codec->name);
	LOGD("codec->long_name: %s", codec->long_name);
	LOGD("codec->type: %d", codec->type);
	LOGD("codec->id: %d", codec->id);
	LOGD("codec->capabilities: %d", codec->capabilities);

	st = avformat_new_stream(dest, codec);
	if (!st) {
		LOGE("ERROR: addStream could not allocate new stream");
		return NULL;
	}
    //avcodec_get_context_defaults3(st->codec, codec);

    st->id = dest->nb_streams-1;
    c = st->codec;
	LOGI("addStream at index %d", st->index);

    // TODO: need this?
	// Some formats want stream headers to be separate.
	if (dest->oformat->flags & AVFMT_GLOBALHEADER) {
	    LOGD("addStream: use separate headers");
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}

	return st;
}

// add a video stream
void addVideoStream(AVFormatContext *dest){
	AVCodecContext *c;

//    AVCodec codec = {0};
//    codec.type = AVMEDIA_TYPE_VIDEO;

	AVStream *st = addStream(dest, VIDEO_CODEC_ID, NULL);
	videoStreamIndex = st->index;
    c = st->codec;

    // video parameters
	c->codec_id = VIDEO_CODEC_ID;
	c->pix_fmt = VIDEO_PIX_FMT;
	c->width = videoWidth;
	c->height = videoHeight;
	c->bit_rate = videoBitRate;

	// timebase: This is the fundamental unit of time (in seconds) in terms
	// of which frame timestamps are represented. For fixed-fps content,
	// timebase should be 1/framerate and timestamp increments should be
	// identical to 1.
	c->time_base.den = videoFps;
	c->time_base.num = 1;

	// TODO: need this?
	//c->gop_size = 12; // emit one intra frame every twelve frames at most
}

// add an audio stream
void addAudioStream(AVFormatContext *dest){
	AVCodecContext *c;

	AVStream *st = addStream(dest, AUDIO_CODEC_ID, NULL);
	audioStreamIndex = st->index;
    c = st->codec;

	// audio parameters
	// TODO: keep this FF_COMPLIANCE_UNOFFICIAL?
	c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL; // for native aac support
	c->sample_fmt  = AUDIO_SAMPLE_FMT;
	c->sample_rate = audioSampleRate;
	c->channels    = audioNumChannels;
	c->bit_rate    = audioBitRate;
    //c->time_base.num = 1;
    //c->time_base.den = c->sample_rate;
}

// get the format context based on the format name and output path
AVFormatContext* avFormatContextForOutputPath(const char *path, const char *formatName){
    AVFormatContext *outputFormatContext;
    AVOutputFormat *fmt;

    LOGI("avFormatContextForOutputPath format: %s path: %s", formatName, path);
    int openOutputValue = avformat_alloc_output_context2(&outputFormatContext, NULL, formatName, path);
    if (openOutputValue < 0) {
        LOGE("Error getting format context for output path: %s", av_err2str(openOutputValue));
    }

    fmt = outputFormatContext->oformat;
    fmt->video_codec = VIDEO_CODEC_ID;
    fmt->audio_codec = AUDIO_CODEC_ID;

    LOGD("fmt->name: %s", fmt->name);
    LOGD("fmt->long_name: %s", fmt->long_name);
    LOGD("fmt->mime_type: %s", fmt->mime_type);
    LOGD("fmt->extensions: %s", fmt->extensions);
    LOGD("fmt->audio_codec: %d", fmt->audio_codec);
    LOGD("fmt->video_codec: %d", fmt->video_codec);
    LOGD("fmt->subtitle_codec: %d", fmt->subtitle_codec);
    LOGD("fmt->flags: %d", fmt->flags);

    return outputFormatContext;
}

// open a file for writing
int openForWriting(AVFormatContext *avfc, const char *path){
    int openForWritingResult = 0;

    if (!(avfc->oformat->flags & AVFMT_NOFILE)) {
        LOGI("Opening output file for writing at path %s", path);
        openForWritingResult = avio_open(&avfc->pb, path, AVIO_FLAG_WRITE);
        if (openForWritingResult < 0) {
            LOGE("Could not open '%s': %s", path, av_err2str(openForWritingResult));
        }
        return openForWritingResult;
    } else {
        LOGD("this format does not require a file");
        return 0;
    }
}

// write the header into the output file
int writeHeader(AVFormatContext *avfc){
    LOGD("output format: %s", avfc->oformat->name);
    LOGD("i/o context: %p", avfc->pb);
    int writeHeaderResult = avformat_write_header(avfc, NULL);
    if (writeHeaderResult < 0) {
        LOGE("Error writing header: %s", av_err2str(writeHeaderResult));
    } else {
        LOGI("Wrote file header result: %d", writeHeaderResult);
    }
    return writeHeaderResult;
}

// write the trailer into the output file
int writeTrailer(AVFormatContext *avfc){
    LOGI("Writing trailer ...");
    return av_write_trailer(avfc);
}


//
//-- JNI interface
//

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_setAVOptions
  (JNIEnv *env, jobject jThis, jobject jOpts) {
    LOGD("setAVOptions");

    // get the java object fields
	jclass ClassAVOptions = (*env)->GetObjectClass(env, jOpts);

	jfieldID jOutputFormatName = (*env)->GetFieldID(env, ClassAVOptions, "outputFormatName", "Ljava/lang/String;");

	jfieldID jVideoHeightId = (*env)->GetFieldID(env, ClassAVOptions, "videoHeight", "I");
	jfieldID jVideoWidthId = (*env)->GetFieldID(env, ClassAVOptions, "videoWidth", "I");
	jfieldID jVideoFpsId = (*env)->GetFieldID(env, ClassAVOptions, "videoFps", "I");
	jfieldID jVideoBitRateId = (*env)->GetFieldID(env, ClassAVOptions, "videoBitRate", "I");

	jfieldID jAudioSampleRateId = (*env)->GetFieldID(env, ClassAVOptions, "audioSampleRate", "I");
	jfieldID jAudioNumChannelsId = (*env)->GetFieldID(env, ClassAVOptions, "audioNumChannels", "I");
	jfieldID jAudioBitRateId = (*env)->GetFieldID(env, ClassAVOptions, "audioBitRate", "I");

    // read the java object fields and initialize our variables from above
    jstring outputFormatNameString = (jstring) (*env)->GetObjectField(env, jOpts, jOutputFormatName);
    outputFormatName = (*env)->GetStringUTFChars(env, outputFormatNameString, NULL);
    // TODO: ReleaseStringUTFChars outputFormatNameString

	videoHeight = (*env)->GetIntField(env, jOpts, jVideoHeightId);
	videoWidth = (*env)->GetIntField(env, jOpts, jVideoWidthId);
	videoFps = (*env)->GetIntField(env, jOpts, jVideoFpsId);
	videoBitRate = (*env)->GetIntField(env, jOpts, jVideoBitRateId);

	audioSampleRate = (*env)->GetIntField(env, jOpts, jAudioSampleRateId);
	audioNumChannels = (*env)->GetIntField(env, jOpts, jAudioNumChannelsId);
	audioBitRate = (*env)->GetIntField(env, jOpts, jAudioBitRateId);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_prepareAVFormatContext
  (JNIEnv * env, jobject self, jstring jOutputPath) {

    LOGD("prepareAVFormatContext");

    init();

    // Create AVRational that expects timestamps in microseconds
    deviceTimeBase = av_malloc(sizeof(AVRational));
    deviceTimeBase->num = 1;
    deviceTimeBase->den = 1000000;

    AVFormatContext *inputFormatContext;
    outputPath = (*env)->GetStringUTFChars(env, jOutputPath, NULL);
    // TODO: ReleaseStringUTFChars jOutputPath

    outputFormatContext = avFormatContextForOutputPath(outputPath, outputFormatName);
    LOGI("post avFormatContextForOutputPath");

    // For manually crafting AVFormatContext
    addVideoStream(outputFormatContext);
    addAudioStream(outputFormatContext);

    int result = openForWriting(outputFormatContext, outputPath);
    if(result < 0){
        LOGE("openForWriting error: %d", result);
    }

    avDumpFormat(outputFormatContext, 0, outputPath, 1);

    writeHeader(outputFormatContext);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_writeVideoHeader
  (JNIEnv * env, jobject self, jbyteArray jData, jint jSize) {

    LOGD("writeVideoHeader");

    jboolean isCopy;
    jbyte* rawjBytes = (*env)->GetByteArrayElements(env, jData, &isCopy);
    int dataSize = (int)jSize;
    char *extDataBuffer;
    AVStream *st;

    // add the extra data to the video codec
    LOGD("Allocating %d bytes for the extra data buffer.", dataSize);
    extDataBuffer = av_malloc(dataSize);
    LOGD("Finding video stream.");
    st = outputFormatContext->streams[videoStreamIndex];
    LOGD("Filling extra data buffer.");
    memcpy(extDataBuffer, rawjBytes, dataSize);
    LOGD("Setting extra data on codec.");
    st->codec->extradata = extDataBuffer;
    st->codec->extradata_size = (int)jSize;

    LOGD("Releasing JNI ByteArray elements.");
    (*env)->ReleaseByteArrayElements(env, jData, rawjBytes, 0);
    return;
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_writeAVPacketFromEncodedData
  (JNIEnv *env, jobject self, jobject jData, jint jIsVideo, jint jOffset,
   jint jSize, jint jFlags, jlong jPts) {

    AVStream *st;
    AVCodecContext *c;
    uint8_t *filtered_data;
    int filtered_data_size = 0;

    if (packet == NULL) {
        packet = av_malloc(sizeof(AVPacket));
        if (!packet) {
            LOGE("ERROR trying to av_malloc the AVPacket");
        }
    }

    // jData is a ByteBuffer managed by Android's MediaCodec.
    uint8_t *data = (*env)->GetDirectBufferAddress(env, jData);
    av_init_packet(packet);

	if ( ((int) jIsVideo) == JNI_TRUE) {
		packet->stream_index = videoStreamIndex;
    	videoFrameCount++;
	} else {
		packet->stream_index = audioStreamIndex;
	}

    packet->size = (int) jSize;
    packet->data = data;
    packet->pts = packet->dts = (int) jPts;
    st = outputFormatContext->streams[packet->stream_index];
    c = st->codec;


    // TODO: This code is horrible -- refactor.
    LOGD("Possibly about to filter packet buffer ...");
    if (st->codec->codec_id == AUDIO_CODEC_ID) {
        LOGD("About to filter audio packet buffer ...");
        filtered_data = av_malloc(packet->size);
        LOGD("1 ...");
        int ret = 0;
        AVBitStreamFilterContext* bsfc = av_bitstream_filter_init("aac_adtstoasc");
        if (!bsfc) {
            LOGE("Error creating aac_adtstoasc bitstream filter.");
        }
        LOGD("2 ...");
        ret = av_bitstream_filter_filter(bsfc, st->codec, NULL,
                                         &filtered_data, &filtered_data_size,
                                         packet->data, packet->size,
                                         packet->flags & AV_PKT_FLAG_KEY);
        LOGD("3 ...");
        if (ret < 0) {
            LOGE("Failed to filter bitstream.");
        }
        LOGD("4 ...");
        av_free(packet->data);
        packet->data = filtered_data;
        packet->size = filtered_data_size;
        LOGD("5 ...");
    }

    // rescale the timing information for the packet
    LOGD("time bases: stream=%d/%d, codec=%d/%d, device=%d/%d",
         st->time_base.num, st->time_base.den,
         c->time_base.num, c->time_base.den,
         (*deviceTimeBase).num, (*deviceTimeBase).den);
    packet->pts = av_rescale_q(packet->pts, *deviceTimeBase, st->time_base);
    packet->dts = av_rescale_q(packet->dts, *deviceTimeBase, st->time_base);
    // COPIED: from ffmpeg remuxing.c
    //packet->pts = av_rescale_q_rnd(packet->pts, *deviceTimeBase, st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    //packet->dts = av_rescale_q_rnd(packet->dts, *deviceTimeBase, st->time_base, AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX);
    //packet->duration = av_rescale_q(packet->duration, *deviceTimeBase, st->time_base);
    //packet->pos = -1;

    LOGD("about to write frame to stream %d: (pts=%lld, size=%d)", packet->stream_index, packet->pts, packet->size);
    int writeFrameResult = av_interleaved_write_frame(outputFormatContext, packet);
    if(writeFrameResult < 0){
        LOGE("av_interleaved_write_frame video: %d pkt: %d size: %d error: %s", ((int) jIsVideo), videoFrameCount, ((int) jSize), av_err2str(writeFrameResult));
    }
    LOGD("wrote frame");

    if (st->codec->codec_id == AUDIO_CODEC_ID) {
        av_free(filtered_data);
        LOGD("cleaned up buffer used for filtering");

    }
    av_free_packet(packet);
    LOGD("cleaned up packet");
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_finalizeAVFormatContext
  (JNIEnv *env, jobject self) {

    LOGD("finalizeAVFormatContext");

    int writeTrailerResult = writeTrailer(outputFormatContext);
    if(writeTrailerResult < 0){
        LOGE("av_write_trailer error: %d", writeTrailerResult);
    }

    if (!(outputFormatContext->oformat->flags & AVFMT_NOFILE)) {
        avio_close(outputFormatContext->pb);
    }

    if (outputFormatContext) avformat_free_context(outputFormatContext);
    if (deviceTimeBase) av_free(deviceTimeBase);
}

