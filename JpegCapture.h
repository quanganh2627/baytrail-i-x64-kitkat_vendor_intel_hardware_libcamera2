/*
 * Copyright (c) 2014 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_LIBCAMERA_JPEG_CAPTURE_H
#define ANDROID_LIBCAMERA_JPEG_CAPTURE_H

#include <stdint.h>

#define V4L2_PIX_FMT_CONTINUOUS_JPEG V4L2_PIX_FMT_JPEG

namespace android {

enum JpegFrameType {
    JPEG_FRAME_TYPE_META    = 0x00,
    JPEG_FRAME_TYPE_FULL    = 0x01,
    JPEG_FRAME_TYPE_SPLITED = 0x02
};

const size_t JPEG_INFO_START = 2048; // todo: figure out why this is not 0
const size_t JPEG_INFO_SIZE  = 2048;
const size_t NV12_META_START = JPEG_INFO_START + JPEG_INFO_SIZE;
const size_t NV12_META_SIZE  = 4096;
const size_t JPEG_META_START = NV12_META_START + NV12_META_SIZE;
const size_t JPEG_META_SIZE  = 4096;
const size_t JPEG_DATA_START = JPEG_META_START + JPEG_META_SIZE;
const size_t JPEG_DATA_SIZE  = 0x800000;
const size_t JPEG_FRAME_SIZE = JPEG_DATA_START + JPEG_DATA_SIZE;

// JPEG info addresses
const size_t JPEG_MODE_ADDR = 0xF;
const size_t JPEG_COUNT_ADDR = 0x10;
const size_t JPEG_SIZE_ADDR = 0x13;

const int NUM_OF_JPEG_CAPTURE_SNAPSHOT_BUF = 6;

} // namespace

#endif // ANDROID_LIBCAMERA_JPEG_CAPTURE_H
