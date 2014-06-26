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
const char *outputFormatName = "flv";
int audioStreamIndex = -1;
int videoStreamIndex = -1;

// A/V
int VIDEO_PIX_FMT = PIX_FMT_YUV420P;
int VIDEO_CODEC_ID = CODEC_ID_H264;
int videoWidth = 1280;
int videoHeight = 720;
int AUDIO_CODEC_ID = CODEC_ID_AAC;
int AUDIO_SAMPLE_FMT = AV_SAMPLE_FMT_S16;
int audioSampleRate = 44100;
int audioNumChannels = 1;

AVFormatContext *outputFormatContext;
AVStream *audioStream;
AVStream *videoStream;
AVCodec *audioCodec;
AVCodec *videoCodec;
AVRational *videoSourceTimeBase;
AVRational *audioSourceTimeBase;

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
AVStream* addStream(AVFormatContext *dest, enum AVCodecID codecId) {
	AVCodecContext *c;
	AVStream *st;
	AVCodec *codec;

	codec = avcodec_find_encoder(codecId);
	if (!codec) {
		LOGI("addStream codec not found (probably okay)");
//		codec = av_malloc(sizeof(AVCodec));
//		codec->type = AVMEDIA_TYPE_VIDEO;
	}

	st = avformat_new_stream(dest, codec);
	if (!st) {
		LOGE("add_video_stream could not alloc stream");
	}
    st->id = dest->nb_streams-1;
    c = st->codec;
	LOGI("addStream at index %d", st->index);

    // TODO: this is not in muxing.c
	//avcodec_get_context_defaults3(c, codec);

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

	AVStream *st = addStream(dest, VIDEO_CODEC_ID);
	videoStreamIndex = st->index;
    c = st->codec;
    AVCodec codec = {0};
    codec.type = AVMEDIA_TYPE_VIDEO;
    avcodec_get_context_defaults3(c, &codec);

    // video parameters
	c->codec_id = VIDEO_CODEC_ID;
	c->bit_rate = 1500000;
	c->pix_fmt = VIDEO_PIX_FMT;
	c->width = videoWidth;
	c->height = videoHeight;

	// timebase: This is the fundamental unit of time (in seconds) in terms
	// of which frame timestamps are represented. For fixed-fps content,
	// timebase should be 1/framerate and timestamp increments should be
	// identical to 1.
	c->time_base.den = 30;
	c->time_base.num = 1;

	// TODO: need this?
	c->gop_size = 12; // emit one intra frame every twelve frames at most
}

// add an audio stream
void addAudioStream(AVFormatContext *dest){
	AVCodecContext *c;

	AVStream *st = addStream(dest, AUDIO_CODEC_ID);
	audioStreamIndex = st->index;
    c = st->codec;

	// audio parameters
	c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL; // for native aac support
	c->bit_rate    = 128000;
	c->sample_fmt  = AUDIO_SAMPLE_FMT;
	c->sample_rate = audioSampleRate;
	c->channels    = audioNumChannels;
	c->time_base.num = 1;
	c->time_base.den = c->sample_rate;
}

// get the format context based on the format name and output path
AVFormatContext* avFormatContextForOutputPath(const char *path, const char *formatName){
    AVFormatContext *outputFormatContext;
    LOGI("avFormatContextForOutputPath format: %s path: %s", formatName, path);
    int openOutputValue = avformat_alloc_output_context2(&outputFormatContext, NULL, formatName, path);
    if (openOutputValue < 0) {
        LOGE("Error getting format context for output path: %s", av_err2str(openOutputValue));
    }
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
	jfieldID jVideoHeightId = (*env)->GetFieldID(env, ClassAVOptions, "videoHeight", "I");
	jfieldID jVideoWidthId = (*env)->GetFieldID(env, ClassAVOptions, "videoWidth", "I");
	jfieldID jAudioSampleRateId = (*env)->GetFieldID(env, ClassAVOptions, "audioSampleRate", "I");
	jfieldID jNumAudioChannelsId = (*env)->GetFieldID(env, ClassAVOptions, "audioNumChannels", "I");

    // read the java object fields and initialize our variables from above
	videoHeight = (*env)->GetIntField(env, jOpts, jVideoHeightId);
	videoWidth = (*env)->GetIntField(env, jOpts, jVideoWidthId);
	audioSampleRate = (*env)->GetIntField(env, jOpts, jAudioSampleRateId);
	audioNumChannels = (*env)->GetIntField(env, jOpts, jNumAudioChannelsId);
}

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_prepareAVFormatContext
  (JNIEnv * env, jobject self, jstring jOutputPath) {

    LOGD("prepareAVFormatContext");

    init();

    // Create AVRational that expects timestamps in microseconds
//    videoSourceTimeBase = av_malloc(sizeof(AVRational));
//    videoSourceTimeBase->num = 1;
//    videoSourceTimeBase->den = 1000000;
//    audioSourceTimeBase = av_malloc(sizeof(AVRational));
//    audioSourceTimeBase->num = 1;
//    audioSourceTimeBase->den = 1000000;

    AVFormatContext *inputFormatContext;
    outputPath = (*env)->GetStringUTFChars(env, jOutputPath, NULL);

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

JNIEXPORT void JNICALL Java_io_cine_ffmpegbridge_FFmpegBridge_writeAVPacketFromEncodedData
  (JNIEnv *env, jobject self, jobject jData, jint jIsVideo, jint jOffset,
   jint jSize, jint jFlags, jlong jPts) {

    LOGD("writeAVPacketFromEncodedData");

    if (packet == NULL) {
        packet = av_malloc(sizeof(AVPacket));
        LOGI("av_malloc packet");
    }

    if ( ((int) jIsVideo) == JNI_TRUE ){
    	videoFrameCount++;
    }

    // jData is a ByteBuffer managed by Android's MediaCodec.
    uint8_t *data = (*env)->GetDirectBufferAddress(env, jData);
    av_init_packet(packet);

	if ( ((int) jIsVideo) == JNI_TRUE) {
		packet->stream_index = videoStreamIndex;
	} else {
		packet->stream_index = audioStreamIndex;
	}

    packet->size = (int) jSize;
    packet->data = data;
    packet->pts = (int) jPts;

    // rescale the timing information for the packet
	packet->pts = av_rescale_q(packet->pts, *videoSourceTimeBase, (outputFormatContext->streams[packet->stream_index]->time_base));

    int writeFrameResult = av_interleaved_write_frame(outputFormatContext, packet);
    if(writeFrameResult < 0){
        LOGE("av_interleaved_write_frame video: %d pkt: %d size: %d error: %s", ((int) jIsVideo), videoFrameCount, ((int) jSize), av_err2str(writeFrameResult));
    }

    av_free_packet(packet);
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

    avformat_free_context(outputFormatContext);
}

