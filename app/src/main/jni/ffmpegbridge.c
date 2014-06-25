#include <jni.h>
#include <android/log.h>
#include "ffmpegbridge.h"

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

// get a user-friendly error string from an error number
char* stringForAVErrorNumber(int errorNumber){
    char *errorBuffer = malloc(sizeof(char) * AV_ERROR_MAX_STRING_SIZE);

    int strErrorResult = av_strerror(errorNumber, errorBuffer, AV_ERROR_MAX_STRING_SIZE);
    if (strErrorResult != 0) {
        LOGE("av_strerror error: %d", strErrorResult);
        return NULL;
    }
    return errorBuffer;
}

// add a video stream
void addVideoStream(AVFormatContext *dest){
	AVCodecContext *c;
	AVStream *st;
	AVCodec *codec;

	// TODO: this can probably be removed
	codec = avcodec_find_encoder(VIDEO_CODEC_ID);
	if (!codec) {
		LOGI("add_video_stream codec not found, as expected. No encoding necessary");
	}

	st = avformat_new_stream(dest, codec);
	if (!st) {
		LOGE("add_video_stream could not alloc stream");
	}

	videoStreamIndex = st->index;
	LOGI("addVideoStream at index %d", videoStreamIndex);
	c = st->codec;

	avcodec_get_context_defaults3(c, codec);

	c->codec_id = VIDEO_CODEC_ID;

    // video parameters
   	// TODO: need bit_rate here?
	//c->bit_rate = 1500000;
	c->width = videoWidth;
	c->height = videoHeight;

	// timebase: This is the fundamental unit of time (in seconds) in terms
	// of which frame timestamps are represented. For fixed-fps content,
	// timebase should be 1/framerate and timestamp increments should be
	// identical to 1.
	c->time_base.den = 30;
	c->time_base.num = 1;
	// TODO: need this?
	// emit one intra frame every twelve frames at most
	//c->gop_size      = 12;
	c->pix_fmt       = VIDEO_PIX_FMT;

    // TODO: need this?
	// Some formats want stream headers to be separate.
	if (dest->oformat->flags & AVFMT_GLOBALHEADER) {
	    LOGD("addVideoStream: use separate headers");
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
	}
}

// add an audio stream
void addAudioStream(AVFormatContext *formatContext){
	AVCodecContext *c;
	AVStream *st;
	AVCodec *codec;

	// TODO: this can probably be removed
	codec = avcodec_find_encoder(AUDIO_CODEC_ID);
	if (!codec) {
		LOGE("add_audio_stream codec not found");
	}
	LOGI("add_audio_stream found codec_id: %d", codec->id);
	st = avformat_new_stream(formatContext, codec);
	if (!st) {
		LOGE("add_audio_stream could not alloc stream");
	}

	audioStreamIndex = st->index;
	LOGI("addAudioStream at index %d", audioStreamIndex);

   	// TODO: what is st->id?
	//st->id = 1;
	c = st->codec;
	avcodec_get_context_defaults3(c, codec);
	c->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL; // for native aac support

	// audio parameters
   	// TODO: need bit_rate here?
	//c->bit_rate    = bit_rate;
	c->sample_fmt  = AUDIO_SAMPLE_FMT;
	c->sample_rate = audioSampleRate;
	c->channels    = audioNumChannels;
	c->time_base.num = 1;
	c->time_base.den = c->sample_rate;
	LOGI("addAudioStream sample_rate %d", c->sample_rate);
	//LOGI("add_audio_stream parameters: sample_fmt: %d bit_rate: %d sample_rate: %d", codec_audio_sample_fmt, bit_rate, audio_sample_rate);

    // TODO: need this?
	// Some formats want stream headers to be separate.
	if (formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
		LOGD("addVideoStream: use separate headers");
		c->flags |= CODEC_FLAG_GLOBAL_HEADER;
    }
}

// get the format context based on the format name and output path
AVFormatContext* avFormatContextForOutputPath(const char *path, const char *formatName){
    AVFormatContext *outputFormatContext;
    LOGI("avFormatContextForOutputPath format: %s path: %s", formatName, path);
    int openOutputValue = avformat_alloc_output_context2(&outputFormatContext, NULL, formatName, path);
    if (openOutputValue < 0) {
        avformat_free_context(outputFormatContext);
    }
    return outputFormatContext;
}

// open a file for writing
int openForWriting(AVFormatContext *avfc, const char *path){
    if (!(avfc->oformat->flags & AVFMT_NOFILE)) {
        LOGI("Opening output file for writing at path %s", path);
        return avio_open(&avfc->pb, path, AVIO_FLAG_WRITE);
    }
    LOGD("this format does not require a file");
    return 0;
}

// write the header into the output file
int writeHeader(AVFormatContext *avfc){
    AVDictionary *options = NULL;

    int writeHeaderResult = avformat_write_header(avfc, &options);
    if (writeHeaderResult < 0) {
        LOGE("Error writing header: %s", stringForAVErrorNumber(writeHeaderResult));
        av_dict_free(&options);
    }
    LOGI("Wrote file header");
    av_dict_free(&options);
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
    videoSourceTimeBase = av_malloc(sizeof(AVRational));
    videoSourceTimeBase->num = 1;
    videoSourceTimeBase->den = 1000000;
    audioSourceTimeBase = av_malloc(sizeof(AVRational));
	audioSourceTimeBase->num = 1;
	audioSourceTimeBase->den = 1000000;

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
        LOGE("av_interleaved_write_frame video: %d pkt: %d size: %d error: %s", ((int) jIsVideo), videoFrameCount, ((int) jSize), stringForAVErrorNumber(writeFrameResult));
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
}


