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

#ifndef ANDROID_HARDWARE_INTEL_CAMERA_SOC_H
#define ANDROID_HARDWARE_INTEL_CAMERA_SOC_H

#include <utils/threads.h>
#include <camera/CameraHardwareInterface.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "v4l2SOC.h"
#include "ccrgb16toyuv420sp.h"

#ifdef __cplusplus
}
#endif

namespace android {

// value to ColorEffect 
#define V4L2_COLORFX_NONE		0
#define V4L2_COLORFX_BW			1
#define V4L2_COLORFX_SEPIA		2
#define V4L2_COLORFX_NEGATIVE		3
#define V4L2_COLORFX_EMBOSS		4
#define V4L2_COLORFX_SKETCH		5
#define V4L2_COLORFX_SKY_BLUE		6
#define V4L2_COLORFX_GRASS_GREEN	7
#define V4L2_COLORFX_SKIN_WHITE		8
#define V4L2_COLORFX_VIVID		9
#define V4L2_COLORFX_MONO		10
#define V4L2_COLORFX_SOLARIZE		11
// value to WhiteBalance
#define SENSOR_AWB_AUTO			0x00000001
#define SENSOR_AWB_OFF			0x00000002
#define SENSOR_AWB_DAYLIGHT		0x00000004
#define SENSOR_AWB_CLOUDY_DAYLIGHT	0x00000008
#define SENSOR_AWB_INCANDESCENT		0x00000010
#define SENSOR_AWB_FLUORESCENT		0x00000020
// value to Exposure
#define EXPOSURE_COMPENSATION		0
#define MAX_EXPOSURE_COMPENSATION	3
#define MIN_EXPOSURE_COMPENSATION	-3
#define EXPOSURE_COMPENSATION_STEP	1
// PictureSize
const char QSXGA_PLUS4[] = "2592x1944";
const char QXGA[]        = "2048x1536";
const char UXGA[]        = "1600x1200";
const char SXGA[]        = "1280x960";
const char XGA[]         = "1024x768";
const char SVGA[]        = "800x600";
const char VGA[]         = "640x480";
const char QVGA[]        = "320x240";
// rotation
const char KEY_SUPPORTED_ROTATIONS[] = "rotation-values";
const char DEGREE_0[]   = "rotation0";
const char DEGREE_90[]  = "rotation90";
const char DEGREE_180[] = "rotation180";
// JPEG QUALITY
const char KEY_SUPPORTED_JPEG_QUALITY[] = "jpeg-quality-values";
const char NORMAL[]      =  "70";
const char FINE[]        =  "80";
const char SUPERFINE[]   =  "90";
// picture format
const char PIX_FMT_JPEG[] = "jpeg";
// preview  & video format
const char PIX_FMT_NV12[]   = "yuv420sp";
const char PIX_FMT_YUYV[]   = "yuv422i-yuyv";
const char PIX_FMT_RGB565[] = "rgb565";
// framerate
const char FPS15[] = "15";
const char FPS30[] = "30";
// touchedfocus mode
const char FOCUS_MODE_TOUCHED[] = "touched";




struct setting_map {
    const char *const key;
    int value;
};
struct parameters {
    char  sensorID[32];
    const struct setting_map *framerate_map;

    const struct setting_map *videoformat_map;
    const struct setting_map *previewformat_map;
    const struct setting_map *previewsize_map;
    const struct setting_map *pictureformat_map;
    const struct setting_map *picturesize_map;

    const struct setting_map *focusmode_map;
    const struct setting_map *flashmode_map;
    const struct setting_map *jpegquality_map;
    const struct setting_map *rotation_map;

    const struct setting_map *effect_map;
    const struct setting_map *wb_map;
    const struct setting_map *exposure_map;

};


class IntelCameraSOC {
public:
    IntelCameraSOC(int camera_id);
    ~IntelCameraSOC();

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

    int setCtrl(const int CID, const int value, const char *key);
    int setExtCtrls(const int CID, const int value, const char *key);

    int getSensorID(char *sensorID);
private:
    void nv12_to_nv21(unsigned char *nv12, unsigned char *nv21, int width, int height);
    void yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb);
    void yuyv422_to_rgb16(unsigned char *buf, unsigned char *rgb, int width, int height);
    void yuyv422_to_yuv420sp(unsigned char *bufsrc, unsigned char *bufdest, int width, int height);
    void yuv422_to_yuv420sp_convert(unsigned char *yuv422, int width, int height, unsigned char *yuv420sp);
    void yuv422_to_yuv420p_convert(unsigned char *yuv422, int width, int height, unsigned char *yuv420p);
    void yuv422_to_RGB565 (unsigned char *yuvs, int width, int height, unsigned char *rgbs);

    void trimRGB565(unsigned char *src, unsigned char* dst,
		int src_width, int src_height,
		int dst_width, int dst_height);
    void trimNV12(unsigned char *src, unsigned char* dst,
		int src_width, int src_height,
		int dst_width, int dst_height);


    /*
    void allocSensorInfos(void);
    void freeSensorInfos(void);

    int getDepth(void);
    int calQBufferFrameSize(int w, int h, int depth);
    int calRealFrameSize(int w, int h, int depth);
    */

    v4l2_struct_t *mCI;

    v4l2_frame_info *mFrameInfos;

    v4l2_frame_format mCurrentFrameFormat;

    /*
    sensor_info_t *mSensorInfo;

    AdvanceProcess *mAdvanceProcess;
    */

    //color converters
    ColorConvertBase *ccRGBtoYUV;
    unsigned char *trimBuffer;

    int mCameraId;
};

}; // namespace android

#endif // ANDROID_HARDWARE_INTEL_CAMERA_H
