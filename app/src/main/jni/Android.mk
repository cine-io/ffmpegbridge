#
# Makefile to build the libffmpegbridge.so library.
#
# Copyright (c) 2014, cine.io. All rights reserved.
#

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := ffmpegbridge
LOCAL_SRC_FILES := ffmpegbridge.c logdump.c
LOCAL_CFLAGS := -I$(LOCAL_PATH)/include -I$(LOCAL_PATH)/../prebuilt/include
LOCAL_LDLIBS += -llog
LOCAL_LDLIBS += -L$(LOCAL_PATH)/../prebuilt/lib
LOCAL_LDLIBS += -lcrypto -lssl -lrtmp-1 -lavcodec-55 -lavdevice-55 -lavfilter-4 -lavformat-55 -lavutil-52 -lswresample-0 -lswscale-2

include $(BUILD_SHARED_LIBRARY)
