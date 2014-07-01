//
// The main data structure (and helper functions) used by the FFmpeg bridge.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

#include <android/log.h>

// android logging helpers
#define LOG_TAG "ffmpegbridge"
#define LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...)  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
