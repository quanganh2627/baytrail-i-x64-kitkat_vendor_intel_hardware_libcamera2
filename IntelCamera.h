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

#include <semaphore.h>
#include "v4l2.h"
#include "atomisp_features.h"
#include "CameraAAAProcess.h"
#include "atomisp_config.h"

    /* Define the default parameter for the camera */

#ifdef __cplusplus
}
#endif

namespace android {

typedef struct __IspSettings
{
    int contrast;               // FIX Un.8
    int brightness;
    bool inv_gamma;          // inversed gamma flag, used in negative effect
} IspSettings;

class IntelCamera {
public:
    IntelCamera();
    ~IntelCamera();

    static IntelCamera* createInstance(void) {
        static IntelCamera singleton;
        return &singleton;
    }

    int initCamera(int camera_id);
    int deinitCamera(void);

    //File Input
    int initFileInput();
    int deInitFileInput();
    int configureFileInput(const struct file_input *image);

    //Preview
    int startCameraPreview();
    void stopCameraPreview();
    int getPreview(void **data);
    int putPreview(int index);
    int setPreviewSize(int width, int height, int fourcc);
    int getPreviewSize(int *width, int *height, int *frame_size, int *padded_size);
    int getPreviewPixelFormat(void);
    void setPreviewUserptr(int index, void *addr);

    //Postview
    int setPostViewSize(int width, int height, int fourcc);
    int getPostViewSize(int *width, int *height, int *frame_size);
    int getPostViewPixelFormat(void);

    //Snapshot
    int startSnapshot();
    void stopSnapshot();
    int getSnapshot(void **main_out, void **postview, void *postview_rgb565);
    int putSnapshot(int index);
    int setSnapshotSize(int width, int height, int fourcc);
    int getSnapshotSize(int *width, int *height, int *frame_size);
    int getSnapshotPixelFormat(void);
    void setSnapshotUserptr(int index, void *pic_addr, void *pv_addr);
    void releasePostviewBcd();
    int SnapshotPostProcessing(void *img_data);

    //recorder
    int startCameraRecording();
    void stopCameraRecording();
    int getRecording(void **main_out, void **preview_out);
    int putRecording(int index);
    int setRecorderSize(int width, int height, int fourcc);
    int getRecorderSize(int *width, int *height, int *frame_size, int *padded_size);
    int getRecorderPixelFormat(void);
    void setRecorderUserptr(int index, void *preview, void *recorder);
    int updateRecorderUserptr(int num, unsigned char *recorder[]);

    //3A
    void runAeAfAwb(void);
    bool runStillAfSequence();
    void setStillAfStatus(bool status);

    int get_num_buffers(void);

    // Flash
    void setFlash(void);
    void clearFlash(void);
    void getFlashStatus(bool *flash_status);
    void setFlashStatus(bool flash_status);
    void setIndicatorIntensity(int percent_time_100);
    void setAssistIntensity(int percent_time_100);
    void setFlashMode(int mode);
    int getFlashMode();
    int calculateLightLevel();
    // ISP related settings
    int setColorEffect(int effect);
    int setXNR(bool on);
    int setTNR(bool on);
    int setNREE(bool on);
    int setMACC(int macc);

    // 3A
    sem_t semAAA;

    // Parameter settings
    int get_zoom_val(void);
    int set_zoom_val(int zoom);
    AAAProcess * getmAAA(void);

    // RGB565 color space conversion
    void toRGB565(int width, int height, int fourcc, void *src, void *dst);
private:
    int     createBufferPool(int device, int buffer_count);
    void    destroyBufferPool(int device);
    int     activateBufferPool(int device);
    int     putDualStreams(int index);
    void    update3Aresults(void);
    void    yuv_to_rgb16(unsigned char y,unsigned char u, unsigned char v, unsigned char *rgb);
    void    yuv420_to_rgb565(int width, int height, unsigned char *yuv420, unsigned short *rgb565);
    void    nv12_to_rgb565(int width, int height, unsigned char *nv12, unsigned char *rgb565);

    // Device control
    int openDevice(int mode);
    void closeDevice(void);
    int configureDevice(int device, int w, int h, int fourcc);
    int startCapture(int device, int buffer_count);
    void stopCapture(int device);
    void stopDualStreams(void);
    int grabFrame(int device);
    int resetCamera(void);
    int set_capture_mode(int mode);
    int trimRecordingBuffer(void *main);
    void trimNV12(unsigned char *src, unsigned char* dst, int src_width, int src_height,
                           int dst_width, int dst_height);
    void trimRGB565(unsigned char *src, unsigned char* dst,
                             int src_width, int src_height,
                             int dst_width, int dst_height);

    inline int      m_frameSize(int format, int width, int height);
    int      m_paddingWidth(int format, int width, int height);

    int             m_flag_camera_start[V4L2_DEVICE_NUM];
    int             m_flag_init;
    int             m_camera_id;

    // Frame width, hight and size
    int             m_preview_v4lformat;
    int             m_preview_width;
    int             m_preview_height;
    int             m_preview_max_width;
    int             m_preview_max_height;
    int             m_preview_pad_width;

    int             m_postview_v4lformat;
    int             m_postview_width;
    int             m_postview_height;

    int             m_snapshot_v4lformat;
    int             m_snapshot_width;
    int             m_snapshot_height;
    int             m_snapshot_max_width;
    int             m_snapshot_max_height;
    int             m_snapshot_pad_width;

    int             m_recorder_v4lformat;
    int             m_recorder_width;
    int             m_recorder_height;
    int             m_recorder_max_width;
    int             m_recorder_max_height;
    int             m_recorder_pad_width;

    int             current_w[V4L2_DEVICE_NUM];
    int             current_h[V4L2_DEVICE_NUM];
    int             current_v4l2format[V4L2_DEVICE_NUM];

    //For V4l2
    int             num_buffers; //How many buffer request
    int             run_mode; //ISP run mode
    struct v4l2_buffer_pool v4l2_buf_pool[V4L2_DEVICE_NUM]; //pool[0] for device0 pool[1] for device1
    struct v4l2_buffer_pool v4l2_buf_pool_reserve[V4L2_DEVICE_NUM];
    struct v4l2_capability cap;
    struct v4l2_streamparm parm;
    int             video_fds[V4L2_DEVICE_NUM];
    int             main_fd;

    //flash
    void runPreFlashSequence (void);
    void captureFlashOff(void);
    void captureFlashOnCertainDuration(int mode, int duration, int percent_time_100);
    mutable Mutex       mFlashLock;
    bool        mFlashNecessary;
    bool        mFlashForCapture;
    int mFlashMode;

    //still AF
    mutable Mutex       mStillAfLock;
    bool            mStillAfRunning;
    static const int mStillAfMaxCount = 100;
    mutable Condition   mStillAfCondition;

    //this function will compare saved parameters
    //to default value. Set to isp if they diff since
    //isp is reset.
    int flushISPParameters();

    //Save parameters here
    int	zoom_val;
    int mMacc;
    bool mNrEeOn;
    bool mXnrOn;
    bool mTnrOn;
    int mColorEffect;

    bool mInitGamma;    // true if Gamma table in user space has been initialized

    int set_zoom_val_real(int zoom);
    AAAProcess *mAAA;
    IspSettings mIspSettings;	// ISP related settings

};

}; // namespace android

#endif // ANDROID_HARDWARE_INTEL_CAMERA_H
