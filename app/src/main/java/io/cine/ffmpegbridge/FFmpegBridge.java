//
// Copyright (c) 2014, cine.io. All rights reserved.
//

package io.cine.ffmpegwrapper;

import java.nio.ByteBuffer;

import android.util.Log;

/**
 * Copyright (c) 2014, cine.io. All rights reserved.
 *
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
    public native void writeAVPacketFromEncodedData(ByteBuffer jData, int jIsVideo, int jOffset, int jSize, int jFlags, long jPts);
    public native void finalizeAVFormatContext();

    /**
     * Used to configure the muxer's options.
     * Note the name of this class's fields
     * have to be hardcoded in the native method
     * for retrieval.
     */
    static public class AVOptions{
        int videoHeight = 1280;
        int videoWidth = 720;

        int audioSampleRate = 44100;
        int audioNumChannels = 1;
    }
}
