//
// JNI FFmpeg bridge for muxing H.264 and AAC streams into an FLV container
// for streaming over RTMP from an Android device.
//
// This file requires that libffmpegbridge.so is installed in src/main/jniLibs.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

package io.cine.ffmpegbridge;

import java.nio.ByteBuffer;

import android.util.Log;

/**
 * A bridge to the FFmpeg C libraries.
 *
 * Based on: http://ffmpeg.org/doxygen/trunk/muxing_8c_source.html
 * Inspired by: http://www.roman10.net/how-to-build-android-applications-based-on-ffmpeg-by-an-example/
 *              https://github.com/OnlyInAmerica/FFmpegTest
 *
 * As this is designed to complement Android's MediaCodec class, the only supported formats for
 * jData in writeAVPacketFromEncodedData are: H264 (YUV420P pixel format) / AAC (16 bit signed
 * integer samples, one center channel)
 *
 * Methods of this class must be called in the following order:
 * 1. (optional) setAVOptions
 * 2. prepareAVFormatContext
 * 3. (repeat for each packet) writeAVPacketFromEncodedData
 * 4. finalizeAVFormatContext
 */
public class FFmpegBridge {

    static {
        System.loadLibrary("ffmpegbridge");
    }

    public native void setAVOptions(AVOptions jOpts);
    public native void prepareAVFormatContext(String jOutputPath);
    public native void writeVideoHeader(byte[] jData, int jSize);
    public native void writeAVPacketFromEncodedData(ByteBuffer jData, int jIsVideo, int jOffset, int jSize, int jFlags, long jPts);
    public native void finalizeAVFormatContext();

    /**
     * Used to configure the muxer's options. Note the name of this class's
     * fields have to be hardcoded in the native method for retrieval.
     */
    static public class AVOptions{
        String outputFormatName = "flv";

        int videoHeight = 1280;
        int videoWidth = 720;
        int videoFps = 30;
        int videoBitRate = 1500000;

        int audioSampleRate = 44100;
        int audioNumChannels = 1;
        int audioBitRate = 128000;
    }
}
