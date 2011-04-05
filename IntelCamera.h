/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#ifndef ANDROID_HARDWARE_INTEL_CAMERA_H
#define ANDROID_HARDWARE_INTEL_CAMERA_H

#include <utils/threads.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "v4l2.h"
#include "ccrgb16toyuv420sp.h"
#include "atomisp_features.h"

#ifdef __cplusplus
}
#endif

namespace android {

class IntelCamera {
public:
    IntelCamera();
    ~IntelCamera();

    int captureOpen(int *fd);

    void captureInit(unsigned int width,
                     unsigned int height,
                     v4l2_frame_format frame_fmt,
                     unsigned int frame_num,
                     enum v4l2_memory mem_type,
                     int camera_id);
    void captureFinalize(void);
    void captureStart(void);
    void captureStop(void);

    void captureMapFrame(void);
    void captureUnmapFrame(void);

    void captureSetPtr(unsigned int frame_size, void **ptrs);
    void captureUnsetPtr(void);

    unsigned int captureGrabFrame(void);
    unsigned int captureGetFrame(void *buffer);
    void captureFlashOff(void);
    void captureFlashOnCertainDuration(int mode, int smode, int duration, int intensity);
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    unsigned int captureGetFrameID(void);
#endif
    unsigned int captureGetRecordingFrame(void *buffer, int buffer_share);
    void captureRecycleFrame(void);

#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
    void captureRecycleFrameWithFrameId(unsigned int id);
#endif

    unsigned int get_frame_num(void);
    void get_frame_id(unsigned int *frame_id, unsigned int frame_num);

    int get_device_fd(void);
    int get_zoom_val(void);
    int set_zoom_val(int zoom);
    int set_capture_mode(int mode);
private:
    void nv12_to_nv21(unsigned char *nv12, unsigned char *nv21, int width, int height);
    void yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb);
    void yuyv422_to_rgb16(unsigned char *buf, unsigned char *rgb, int width, int height);
    void yuyv422_to_yuv420sp(unsigned char *bufsrc, unsigned char *bufdest, int width, int height);

    void trimRGB565(unsigned char *src, unsigned char* dst,
                    int src_width, int src_height,
                    int dst_width, int dst_height);
    void trimNV12(unsigned char *src, unsigned char* dst,
                  int src_width, int src_height,
                  int dst_width, int dst_height);


    v4l2_struct_t *mCI;

    v4l2_frame_info *mFrameInfos;

    v4l2_frame_format mCurrentFrameFormat;

    //color converters
    ColorConvertBase *ccRGBtoYUV;
    unsigned char *trimBuffer;
    int	zoom_val;
};

}; // namespace android

#endif // ANDROID_HARDWARE_INTEL_CAMERA_H
