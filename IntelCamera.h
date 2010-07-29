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

#include "ci.h"
#include "ci_adv.h"

#ifdef __cplusplus
}
#endif

//#define RECYCLE_WHEN_RELEASING_RECORDING_FRAME

namespace android {

#define SNR_NAME_LEN    50
#define RES_TEXT_LEN    20

/*Zheng*/
typedef enum {
        SENSOR_INPUT_MIPI,
        SENSOR_INPUT_PARALLEL
} sensor_input_t;

typedef struct _resolution {
        unsigned short width;
        unsigned short height;
} resolution_t;

typedef struct _sensor_res {
	ci_resolution res;
} sensor_res_t;

typedef void *  sensor_dev_t;

typedef struct _sensor_info {
        char            name[SNR_NAME_LEN];
	ci_sensor_num   snr_id;
	unsigned int    type;
	sensor_input_t  input;
        sensor_res_t    **resolutions;
        unsigned int    res_num;
} sensor_info_t;

typedef struct _ci_struct {
        int             major_version;
        int             minor_version;
        ci_context_id           context;
	ci_sensor_num           snr_id;
	ci_device_id            isp_dev;
	ci_device_id            isp_dev_self;
	ci_device_id            snr_dev;
	unsigned short          snr_width;
	unsigned short          snr_height;
	unsigned short          fm_width;
	unsigned short          fm_height;
	unsigned short          continous_af;

	ci_isp_frame_id         *frames;
	ci_isp_frame_id         *frames_self;
	unsigned int            frame_num;
	unsigned int            max_lock_frame_num;
	unsigned int            cur_frame;
	unsigned int            frame_size;
	unsigned int            frame_size_self;

	unsigned int    *buf_status;
} ci_struct_t;

typedef struct __intel_fmt_list {
	unsigned long fourcc;
        int depth;
} intel_fmt_list_t;

typedef struct pref_map {
        const char *const android_value;
        int intel_value;
} pref_map_t;

typedef struct _adv_param {
	resolution_t res;
	ci_wb_param wb_param;
	ci_af_param af_param;
	ci_ae_param ae_param;
} adv_param_t;

#define IMAGE_PRCOESS_FLAGS_TYPE_AF 0x02
#define IMAGE_PRCOESS_FLAGS_TYPE_AE 0x04
#define IMAGE_PRCOESS_FLAGS_TYPE_AWB 0x08
#define IMAGE_PRCOESS_FLAGS_TYPE_IMAGE_EFFECT 0x10

class AdvanceProcess{
public:
	AdvanceProcess(ci_struct_t *ci_struct, sensor_info_t *snr_info_struct);
	~AdvanceProcess();

	void setAdvanceParams(unsigned int w, unsigned int h);

	void advImageProcessAF(void);
	void advImageProcessAE(void);
	void advImageProcessAWB(void);
	void advSetAF(ci_isp_afss_mode mode);
	void advSetAE(ci_isp_aec_mode mode);
	void advSetAWB(ci_isp_awb_mode mode, ci_isp_awb_sub_mode sub_mode);

	int isFlagDirty(void) {
		Mutex::Autolock lock(&mFlagLock);
		return mImageProcessFlags;
	}

	int isFinishedAE(void) {
		return mFinishedAE;
	}

	int isFinishedAWB(void) {
		return mFinishedAWB;
	}

	int isFinishedAF(void) {
		return mFinishedAF;
	}
private:
	void (AdvanceProcess::*fpImageProcessAF)(void);
	void (AdvanceProcess::*fpImageProcessAE)(void);
	void (AdvanceProcess::*fpImageProcessAWB)(void);
	void imageProcessAFforSOC(void);
	void imageProcessAEforSOC(void);
	void imageProcessAWBforSOC(void);
	void imageProcessAFforRAW(void);
	void imageProcessAEforRAW(void);
	void imageProcessAWBforRAW(void);
	
	void (AdvanceProcess::*fpSetAF)(ci_isp_afss_mode mode);
	void (AdvanceProcess::*fpSetAE)(ci_isp_aec_mode mode);
	void (AdvanceProcess::*fpSetAWB)(ci_isp_awb_mode mode, ci_isp_awb_sub_mode sub_mode);
	void setAFforSOC(ci_isp_afss_mode mode);
	void setAEforSOC(ci_isp_aec_mode mode);
	void setAWBforSOC(ci_isp_awb_mode mode, ci_isp_awb_sub_mode sub_mode);
	void setAFforRAW(ci_isp_afss_mode mode);
	void setAEforRAW(ci_isp_aec_mode mode);
	void setAWBforRAW(ci_isp_awb_mode mode, ci_isp_awb_sub_mode sub_mode);

	inline int isFlagEnabled(unsigned int type) {
		Mutex::Autolock lock(&mFlagLock);
		return mImageProcessFlags & type;
	};

	inline void enableFlag(unsigned int type) {
		Mutex::Autolock lock(&mFlagLock);
		mImageProcessFlags |= type;
	};
	inline void disableFlag(unsigned int type) {
		Mutex::Autolock lock(&mFlagLock);
		mImageProcessFlags &= ~type;
	};

	ci_struct_t *mCI;
	sensor_info_t *mSensorInfo;

	ci_af_param *mParamAF;
	ci_ae_param *mParamAE;
	ci_wb_param *mParamAWB;

	// to cheack if do 3As image process.
	unsigned int mImageProcessFlags;

	// to check if 3As finished.
	int mFinishedAE;
	int mFinishedAWB;
	int mFinishedAF;

	Mutex mFlagLock;
	Mutex mImageProcessLock;
};

class IntelCamera {
public:
    IntelCamera();
    ~IntelCamera();

    void captureInit(unsigned int width,
		    unsigned int height,
		    ci_frame_format frame_fmt,
		    unsigned int frame_num);
    void captureFinalize(void);
    void captureStart(void);

    int captureMapFrame(void);
    void captureUnmapFrame(void);

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

    int isResolutionSupported(int w, int h);
    void getMaxResolution(int *w, int *h);

    sensor_info_t *getSensorInfos(void);
    void printSensorInfos(void);

    int isImageProcessEnabled(void);

    int getPrefMapValue(pref_map_t *map, const char *element);

    int getFrameSize(int w, int h);
    int getRealFrameSize(int w, int h);

    void setAF(const char *value);
    void setAE(const char *value);
    void setAWB(const char *value);
    void setJPEGRatio(const char *value);
    void setColorEffect(const char *value);

    void imageProcessBP(void);
    void imageProcessBL(void);
    void imageProcessAF(void);
    void imageProcessAE(void);
    void imageProcessAWB(void);

    int isImageProcessFinishedAE(void);
    int isImageProcessFinishedAWB(void);
    int isImageProcessFinishedAF(void);

    unsigned int get_frame_num(void);
    void get_frame_id(unsigned int *frame_id, unsigned int frame_num);
private:
    void nv12_to_nv21(unsigned char *nv12, unsigned char *nv21, int width, int height);
    void yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb);
    void yuyv422_to_rgb16(unsigned char *buf, unsigned char *rgb, int width, int height);
    void yuyv422_to_yuv420sp(unsigned char *bufsrc, unsigned char *bufdest, int width, int height);
	
    void allocSensorInfos(void);
    void freeSensorInfos(void);

    int getDepth(void);
    int calQBufferFrameSize(int w, int h, int depth);
    int calRealFrameSize(int w, int h, int depth);

    ci_struct_t *mCI;

    ci_isp_frame_map_info mJpegFrameInfo;
    ci_isp_frame_map_info *mFrameInfos;
    ci_isp_frame_map_info *mFrameInfos_self;

    ci_frame_format mCurrentFrameFormat;

    sensor_info_t *mSensorInfo;

    AdvanceProcess *mAdvanceProcess;
};

}; // namespace android

#endif // ANDROID_HARDWARE_INTEL_CAMERA_H
