/*
 * Copyright (C) 2011 The Android Open Source Project
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

#ifndef ANDROID_LIBCAMERA_ATOM_ISP
#define ANDROID_LIBCAMERA_ATOM_ISP

#include <utils/Timers.h>
#include <utils/Errors.h>
#include <utils/Vector.h>
#include <utils/Errors.h>
#include <utils/threads.h>
#include <utils/String8.h>
#include <camera/CameraParameters.h>
#include "IntelParameters.h"
#include "AtomCommon.h"
#include "IntelMetadataBuffer.h"
#include "AtomAAA.h"

namespace android {

#define MAX_V4L2_BUFFERS    MAX_BURST_BUFFERS
#define MAX_CAMERA_NODES    MAX_CAMERAS + 1
#define EV_MIN -2
#define EV_MAX  2

/**
 *  Minimum resolution of video frames to have DVS ON.
 *  Under this it will be disabled
 **/
#define MIN_DVS_WIDTH   384
#define MIN_DVS_HEIGHT  384
#define LARGEST_THUMBNAIL_WIDTH 320
#define LARGEST_THUMBNAIL_HEIGHT 240
#define CAM_WXH_STR(w,h) STRINGIFY_(w##x##h)
#define CAM_RESO_STR(w,h) CAM_WXH_STR(w,h) // example: CAM_RESO_STR(VGA_WIDTH,VGA_HEIGHT) -> "640x480"

//v4l2 buffer in pool
struct v4l2_buffer_info {
    void *data;
    size_t length;
    int width;
    int height;
    int format;
    int flags; //You can use to to detern the buf status
    struct v4l2_buffer vbuffer;
};

struct v4l2_buffer_pool {
    int active_buffers;
    int width;
    int height;
    int format;
    struct v4l2_buffer_info bufs [MAX_V4L2_BUFFERS];
};

class Callbacks;

class AtomISP {

// public types
public:

// constructor/destructor
public:
    AtomISP(void);
    ~AtomISP();

    status_t init(int camera_id);

// public methods
public:

    void getDefaultParameters(CameraParameters *params, CameraParameters *intel_params);

    status_t configure(AtomMode mode);
    status_t start();
    status_t stop();
    status_t releaseCaptureBuffers();

    inline int getNumBuffers() { return mNumBuffers; }

    status_t getPreviewFrame(AtomBuffer *buff, atomisp_frame_status *frameStatus = NULL);
    status_t putPreviewFrame(AtomBuffer *buff);

    status_t setGraphicPreviewBuffers(const AtomBuffer *buffs, int numBuffs);
    status_t getRecordingFrame(AtomBuffer *buff, nsecs_t *timestamp);
    status_t putRecordingFrame(AtomBuffer *buff);

    status_t setSnapshotBuffers(void *buffs, int numBuffs);
    status_t getSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf,
                         atomisp_frame_status *snapshotStatus = NULL);
    status_t putSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf);

    bool dataAvailable();
    bool isBufferValid(const AtomBuffer * buffer) const;

    status_t setPreviewFrameFormat(int width, int height, int format = 0);
    status_t setPostviewFrameFormat(int width, int height, int format);
    status_t setSnapshotFrameFormat(int width, int height, int format);
    status_t setVideoFrameFormat(int width, int height, int format = 0);
    bool applyISPLimitations(CameraParameters *params, bool dvsEnabled);

    inline int getSnapshotPixelFormat() { return mConfig.snapshot.format; }
    void getVideoSize(int *width, int *height, int *stride);

    status_t setSnapshotNum(int num);

    void getZoomRatios(bool videoMode, CameraParameters *params);
    void getFocusDistances(CameraParameters *params);
    status_t setZoom(int zoom);
    status_t setFlash(int numFrames);
    status_t setFlashIndicator(int intensity);
    status_t setTorch(int intensity);
    status_t setColorEffect(v4l2_colorfx effect);
    status_t getMakerNote(atomisp_makernote_info *info);
    status_t setXNR(bool enable);
    status_t setLightFrequency(FlickerMode flickerMode);
    status_t setLowLight(bool enable);
    status_t setGDC(bool enable);

    status_t setDVS(bool enable);
    status_t getDvsStatistics(struct atomisp_dis_statistics *stats,
                              bool *tryAgain) const;
    status_t setMotionVector(const struct atomisp_dis_vector *vector) const;
    status_t setDvsCoefficients(const struct atomisp_dis_coefficients *coefs) const;
    status_t getIspParameters(struct atomisp_parm *isp_param) const;

    // file input/injection API
    int configureFileInject(const char* fileName, int width, int height, int format, int bayerOrder);
    bool isFileInjectionEnabled(void) const { return mFileInject.active; }

    // camera hardware information
    static int getNumberOfCameras();
    static status_t getCameraInfo(int cameraId, camera_info *cameraInfo);

    float getFrameRate() { return mConfig.fps; }

    /* Acceleration API extensions */
   int loadAccFirmware(void *fw, size_t size, unsigned int *fwHandle);
   int unloadAccFirmware(unsigned int fwHandle);
   int setFirmwareArgument(unsigned int fwHandle, unsigned int num,
                           void *val, size_t size);
   int unsetFirmwareArgument(unsigned int fwHandle, unsigned int num);
   int startFirmware(unsigned int fwHandle);
   int waitForFirmware(unsigned int fwHandle);
   int abortFirmware(unsigned int fwHandle, unsigned int timeout);

   // Enable metadata buffer mode API
   status_t storeMetaDataInBuffers(bool enabled);

// private methods
private:

    void initDriverVersion(void);
    status_t init3A(int cameraId);
    void initFrameConfig(int cameraId);
    status_t initCameraInput(int cameraId);
    void initFileInject(void);

    status_t configurePreview();
    status_t startPreview();
    status_t stopPreview();
    status_t configureRecording();
    status_t startRecording();
    status_t stopRecording();
    status_t configureCapture();
    status_t startCapture();
    status_t stopCapture();

    void runStartISPActions();
    void runStopISPActions();

    status_t allocatePreviewBuffers();
    status_t allocateRecordingBuffers();
    status_t allocateSnapshotBuffers();
    status_t allocateMetaDataBuffers();
    status_t freePreviewBuffers();
    status_t freeRecordingBuffers();
    status_t freeSnapshotBuffers();

    void initMetaDataBuf(IntelMetadataBuffer* metaDatabuf);

    const char* getMaxSnapShotResolution();

    status_t updateLowLight();
    status_t setTorchHelper(int intensity);
    status_t updateCaptureParams();

    int  openDevice(int device);
    void closeDevice(int device);
    status_t v4l2_capture_open(int device);
    status_t v4l2_capture_close(int fd);
    status_t v4l2_capture_querycap(int device, struct v4l2_capability *cap);
    status_t v4l2_capture_s_input(int fd, int index);
    int detectDeviceResolutions();
    int atomisp_set_capture_mode(int deviceMode);
    int v4l2_capture_try_format(int device, int *w, int *h, int *format);
    int configureDevice(int device, int deviceMode, int w, int h, int format, bool raw);
    int v4l2_capture_g_framerate(int fd, float * framerate, int width,
                                          int height, int pix_fmt);
    int v4l2_capture_s_format(int fd, int device, int w, int h, int format, bool raw);
    void stopDevice(int device);
    int v4l2_capture_streamoff(int fd);
    void destroyBufferPool(int device);
    int v4l2_capture_free_buffer(int device, struct v4l2_buffer_info *buf_info);
    int v4l2_capture_release_buffers(int device);
    int v4l2_capture_request_buffers(int device, uint num_buffers);
    int startDevice(int device, int buffer_count);
    int createBufferPool(int device, int buffer_count);
    int v4l2_capture_new_buffer(int device, int index, struct v4l2_buffer_info *buf);
    int activateBufferPool(int device);
    int v4l2_capture_streamon(int fd);
    int v4l2_capture_qbuf(int fd, int index, struct v4l2_buffer_info *buf);
    int grabFrame(int device, struct v4l2_buffer *buf);
    int v4l2_capture_dqbuf(int fd, struct v4l2_buffer *buf);
    int atomisp_set_attribute (int fd, int attribute_num,
                               const int value, const char *name);
    int atomisp_set_zoom (int fd, int zoom);
    int xioctl(int fd, int request, void *arg) const;

    int startFileInject(void);
    int stopFileInject(void);
    status_t fileInjectSetSize(void);

    status_t selectCameraSensor();
    size_t setupCameraInfo();
    int getPrimaryCameraIndex(void) const;

// private types
private:

    static const int MAX_SENSOR_NAME_LENGTH = 32;

    static const int V4L2_MAIN_DEVICE       = 0;
    static const int V4L2_POSTVIEW_DEVICE   = 1;
    static const int V4L2_PREVIEW_DEVICE    = 2;
    static const int V4L2_INJECT_DEVICE     = 3;
    static const int V4L2_LEGACY_VIDEO_PREVIEW_DEVICE = 1;

    /**
     * Maximum number of V4L2 devices node we support
     */
    static const int V4L2_MAX_DEVICE_COUNT  = V4L2_INJECT_DEVICE + 1;

    static const int NUM_DEFAULT_BUFFERS = 9;

    struct FrameInfo {
        int format;     // V4L2 format
        int width;      // Frame width
        int height;     // Frame height
        int padding;    // Frame padding width
        int maxWidth;   // Frame maximum width
        int maxHeight;  // Frame maximum height
        int size;       // Frame size in bytes
    };

    struct Config {
        FrameInfo preview;    // preview
        FrameInfo recording;  // recording
        FrameInfo snapshot;   // snapshot
        FrameInfo postview;   // postview (thumbnail for capture)
        float fps;            // preview/recording (shared)
        int num_snapshot;     // number of snapshots to take
        int zoom;             // zoom value
    };

    struct cameraInfo {
        int androidCameraId; /*!< Index used by android to select this camera. This index is passed
                              *   when the camera HAL is open. Used to differentiate back and front camera
                              */
        int port;            //!< AtomISP port type
        uint32_t index;      //!< V4L2 index
        char name[MAX_SENSOR_NAME_LENGTH];
    };

    enum ResolutionIndex {
        RESOLUTION_VGA = 0,
        RESOLUTION_720P,
        RESOLUTION_1080P,
        RESOLUTION_5MP,
        RESOLUTION_8MP,
        RESOLUTION_14MP,
    };

// private members
private:

    static cameraInfo sCamInfo[MAX_CAMERA_NODES];

    AtomMode mMode;
    Callbacks *mCallbacks;

    int mNumBuffers;
    int mNumPreviewBuffers;
    AtomBuffer *mPreviewBuffers;
    AtomBuffer *mRecordingBuffers;

    void **mClientSnapshotBuffers;
    bool mUsingClientSnapshotBuffers;
    bool mStoreMetaDataInBuffers;

    AtomBuffer mSnapshotBuffers[MAX_BURST_BUFFERS];
    AtomBuffer mPostviewBuffers[MAX_BURST_BUFFERS];
    int mNumPreviewBuffersQueued;
    int mNumRecordingBuffersQueued;
    int mNumCapturegBuffersQueued;
    int mFlashTorchSetting;
    Config mConfig;

    int video_fds[V4L2_MAX_DEVICE_COUNT];
    unsigned int mFrameCounter[V4L2_MAX_DEVICE_COUNT];

    int dumpPreviewFrame(int previewIndex);
    int dumpRecordingFrame(int recordingIndex);
    int dumpSnapshot(int snapshotIndex, int postviewIndex);
    int dumpRawImageFlush(void);
    bool isDumpRawImageReady(void);

    struct v4l2_buffer_pool v4l2_buf_pool[V4L2_MAX_DEVICE_COUNT]; //pool[0] for device0 pool[1] for device1

    struct FileInject {
        String8 fileName;
        bool active;
        unsigned int width;
        unsigned int height;
        unsigned int size;
        int format;
        int bayerOrder;
        char *mappedAddr;
    } mFileInject;

    int mConfigSnapshotPreviewDevice;
    int mConfigRecordingPreviewDevice;
    int mConfigLastDevice;
    int mPreviewDevice;
    int mRecordingDevice;

    int mSessionId; // uniquely identify each session

    SensorType mSensorType;
    AtomAAA *mAAA;
    struct cameraInfo *mCameraInput;

    bool mLowLight;
    int mXnr;

    char *mZoomRatios;

    int mRawDataDumpSize;

}; // class AtomISP

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_ISP
