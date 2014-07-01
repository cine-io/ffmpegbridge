//
// Crude port of av_dump_format into a version that will log through the
// Android logging facilities.
//
// Copyright (c) 2014, cine.io. All rights reserved.
//

#include "libavformat/avformat.h"

void avDumpFormat(AVFormatContext *ic, int index, const char *url, int is_output);
