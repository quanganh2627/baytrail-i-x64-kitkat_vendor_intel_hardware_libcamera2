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

// Forward declaration needed to resolve circular reference between AtomAAA
// and AtomISP objects.
namespace android {

class AtomISP;
};

#include <utils/Timers.h>
#include <utils/Errors.h>
#include <utils/Vector.h>
#include <utils/Errors.h>
#include <utils/threads.h>
#include <utils/String8.h>
#include <camera/CameraParameters.h>
#include "IntelParameters.h"
#include "AtomCommon.h"

#ifdef ENABLE_INTEL_METABUFFER
#include "IntelMetadataBuffer.h"
#endif

#include "AtomAAA.h"
#include "PlatformData.h"
#include "CameraConf.h"
#include "I3AControls.h"

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

struct sensorPrivateData
{
    void *data;
    unsigned int size;
};

class Callbacks;

class AtomISP : public I3AControls {
// FIXME: Only needed for NVM parsing "cameranvm_create()" in AtomAAA
    friend class AtomAAA;

// public types
public:

// constructor/destructor
public:
    explicit AtomISP(const sp<CameraConf>& cfg);
    ~AtomISP();

    status_t initDevice();
    status_t init();

// public methods
public:

    int getCurrentCameraId(void);
    void getDefaultParameters(CameraParameters *params, CameraParameters *intel_params);

    status_t configure(AtomMode mode);
    status_t allocateBuffers(AtomMode mode);
    status_t start();
    status_t stop();
    status_t releaseCaptureBuffers();

    inline int getNumBuffers(bool videoMode) { return videoMode? mNumBuffers : mNumPreviewBuffers; }

    void requestClearDriverState();
    void clearDriverState();

    status_t startOfflineCapture();
    status_t stopOfflineCapture();
    bool isOfflineCaptureRunning() const;
    bool isOfflineCaptureSupported() const;

    status_t getPreviewFrame(AtomBuffer *buff, atomisp_frame_status *frameStatus = NULL);
    status_t putPreviewFrame(AtomBuffer *buff);

    status_t setGraphicPreviewBuffers(const AtomBuffer *buffs, int numBuffs);
    status_t getRecordingFrame(AtomBuffer *buff, nsecs_t *timestamp, atomisp_frame_status *frameStatus);
    status_t putRecordingFrame(AtomBuffer *buff);

    status_t setSnapshotBuffers(void *buffs, int numBuffs);
    status_t getSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf,
                         atomisp_frame_status *snapshotStatus = NULL);
    status_t putSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf);

    bool dataAvailable();
    bool isBufferValid(const AtomBuffer * buffer) const;

    status_t setPreviewFrameFormat(int width, int height, int format = 0);
    status_t setPostviewFrameFormat(int width, int height, int format);
    void getPostviewFrameFormat(int &width, int &height, int &format) const;
    status_t setSnapshotFrameFormat(int width, int height, int format);
    status_t setVideoFrameFormat(int width, int height, int format = 0);
    bool applyISPVideoLimitations(CameraParameters *params, bool dvsEnabled) const;

    inline int getSnapshotPixelFormat() { return mConfig.snapshot.format; }
    void getVideoSize(int *width, int *height, int *stride);
    void getPreviewSize(int *width, int *height, int *stride);

    status_t setSnapshotNum(int num);
    status_t setContCaptureNumCaptures(int numCaptures);
    status_t setContCaptureOffset(int captureOffset);

    void getZoomRatios(bool videoMode, CameraParameters *params);
    void getFocusDistances(CameraParameters *params);
    status_t setZoom(int zoom);
    status_t setFlash(int numFrames);
    status_t setFlashIndicator(int intensity);
    status_t setTorch(int intensity);
    status_t setColorEffect(v4l2_colorfx effect);
    status_t applyColorEffect();
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
    status_t enableFrameSyncEvent(bool enable);
    status_t pollFrameSyncEvent();

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
    int mapFirmwareArgument(void *val, size_t size, unsigned long *ptr);
    int unmapFirmwareArgument(unsigned long val, size_t size);
    int setFirmwareArgument(unsigned int fwHandle, unsigned int num,
                            void *val, size_t size);
    int setMappedFirmwareArgument(unsigned int fwHandle, unsigned int mem,
                                  unsigned long val, size_t size);
    int unsetFirmwareArgument(unsigned int fwHandle, unsigned int num);
    int startFirmware(unsigned int fwHandle);
    int waitForFirmware(unsigned int fwHandle);
    int abortFirmware(unsigned int fwHandle, unsigned int timeout);

    int getLastDevice() { return mConfigLastDevice; }

    // Enable metadata buffer mode API
    status_t storeMetaDataInBuffers(bool enabled);

    /* Sensor related controls */
    int  sensorGetGocusStatus(int *status);
    int  sensorSetExposure(struct atomisp_exposure *exposure);
    int  sensorMoveFocusToPosition(int position);
    int  sensorMoveFocusToBySteps(int steps);
    void sensorGetMotorData(sensorPrivateData *sensor_data);
    void sensorGetSensorData(sensorPrivateData *sensor_data);
    int  sensorGetFocusStatus(int *status);
    int  sensorGetModeInfo(struct atomisp_sensor_mode_data *mode_data);
    int  sensorGetExposureTime(int *exposure_time);
    int  sensorGetAperture(int *aperture);
    int  sensorGetFNumber(unsigned short  *fnum_num, unsigned short *fnum_denom);
    /* ISP related controls */
    int setAicParameter(struct atomisp_parameters *aic_params);
    int setIspParameter(struct atomisp_parm *isp_params);
    int getIspStatistics(struct atomisp_3a_statistics *statistics);
    int setGdcConfig(const struct atomisp_morph_table *tbl);
    int setShadingTable(struct atomisp_shading_table *table);
    int setMaccConfig(struct atomisp_macc_config *macc_cfg);
    int setCtcTable(const struct atomisp_ctc_table *ctc_tbl);
    int setDeConfig(struct atomisp_de_config *de_cfg);
    int setTnrConfig(struct atomisp_tnr_config *tnr_cfg);
    int setEeConfig(struct atomisp_ee_config *ee_cfg);
    int setNrConfig(struct atomisp_nr_config *nr_cfg);
    int setDpConfig(struct atomisp_dp_config *dp_cfg);
    int setWbConfig(struct atomisp_wb_config *wb_cfg);
    int setObConfig(struct atomisp_ob_config *ob_cfg);
    int set3aConfig(const struct atomisp_3a_config *cfg);
    int setGammaTable(const struct atomisp_gamma_table *gamma_tbl);
    int setFpnTable(struct v4l2_framebuffer *fb);
    int setGcConfig(const struct atomisp_gc_config *gc_cfg);
    /* Flash related controls */
    int setFlashIntensity(int intensity);
    /* file injection controls */
    void getSensorDataFromFile(const char *file_name, sensorPrivateData *sensor_data);

    // I3AControls
    virtual void getDefaultParams(CameraParameters *params, CameraParameters *intel_params);
    virtual status_t setEv(float bias);
    virtual status_t getEv(float *ret);
    virtual status_t setAeSceneMode(SceneMode mode);
    virtual SceneMode getAeSceneMode();
    virtual status_t setAwbMode(AwbMode mode);
    virtual AwbMode getAwbMode();
    virtual status_t setManualIso(int iso);
    virtual status_t getManualIso(int *ret);
    virtual status_t setAeMeteringMode(MeteringMode mode);
    virtual MeteringMode getAeMeteringMode();
    virtual status_t set3AColorEffect(v4l2_colorfx effect);

// public static methods
public:
   // return zoom ratio multiplied by 100 from given zoom value
   static int zoomRatio(int zoomValue);

// private types
private:

    static const int MAX_SENSOR_NAME_LENGTH = 32;

    static const int V4L2_MAIN_DEVICE       = 0;
    static const int V4L2_POSTVIEW_DEVICE   = 1;
    static const int V4L2_PREVIEW_DEVICE    = 2;
    static const int V4L2_INJECT_DEVICE     = 3;
    static const int V4L2_ISP_SUBDEV        = 4;
    static const int V4L2_LEGACY_VIDEO_PREVIEW_DEVICE = 1;

    /**
     * Maximum number of V4L2 devices node we support
     */
    static const int V4L2_MAX_DEVICE_COUNT  = V4L2_ISP_SUBDEV + 1;

    static const int NUM_DEFAULT_BUFFERS = 9;

    static const int NUM_PREVIEW_BUFFERS = 6;

    struct FrameInfo {
        int format;     // V4L2 format
        int width;      // Frame width
        int height;     // Frame height
        int stride;     // Frame stride (can be bigger than width)
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

    struct ContinuousCaptureConfig {
      int numCaptures;        /*!< Number of captures
                               * -1 = capture continuously
                               * 0 = disabled, stop captures
                               * >0 = burst of N snapshots
                               */
      int offset;             /*!< burst start offset */
      unsigned int skip;      /*!< skip factor */
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
        RESOLUTION_3MP,
        RESOLUTION_5MP,
        RESOLUTION_8MP,
        RESOLUTION_14MP,
    };

    enum DeviceState {
        DEVICE_CLOSED = 0,  /*!< kernel device closed */
        DEVICE_OPEN,        /*!< device node opened */
        DEVICE_CONFIGURED,  /*!< device format set, IOC_S_FMT */
        DEVICE_PREPARED,    /*!< buffers queued, IOC_QBUF */
        DEVICE_STARTED,     /*!< stream started, IOC_STREAMON */
        DEVICE_ERROR        /*!< undefined state */
    };

// private methods
private:

    status_t initCameraInput();
    void initFileInject();
    void initDriverVersion(void);
    void initFrameConfig();
    status_t init3A();

    status_t configurePreview();
    status_t startPreview();
    status_t stopPreview();
    status_t configureRecording();
    status_t startRecording();
    status_t stopRecording();
    status_t configureCapture();
    status_t configureContinuous();
    status_t startCapture();
    status_t stopCapture();
    status_t startContinuousPreview();
    status_t stopContinuousPreview();

    status_t requestContCapture(int numCaptures, int offset, unsigned int skip);

    void runStartISPActions();
    void runStopISPActions();

    status_t allocatePreviewBuffers();
    status_t allocateRecordingBuffers();
    status_t allocateSnapshotBuffers();
    status_t allocateMetaDataBuffers();
    status_t freePreviewBuffers();
    status_t freeRecordingBuffers();
    status_t freeSnapshotBuffers();

#ifdef ENABLE_INTEL_METABUFFER
    void initMetaDataBuf(IntelMetadataBuffer* metaDatabuf);
#endif

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
    int configureDevice(int device, int deviceMode, FrameInfo* fInfo, bool raw);
    int v4l2_capture_g_framerate(int fd, float * framerate, int width,
                                          int height, int pix_fmt);
    int v4l2_capture_s_format(int fd, int device, int w, int h, int format, bool raw, int* stride);
    int stopDevice(int device, bool leaveConfigured = false);
    int v4l2_capture_streamoff(int fd);
    void destroyBufferPool(int device);
    int v4l2_capture_free_buffer(int device, struct v4l2_buffer_info *buf_info);
    int v4l2_capture_release_buffers(int device);
    int v4l2_capture_request_buffers(int device, uint num_buffers);
    int prepareDevice(int device, int buffer_count);
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
    int atomisp_get_attribute (int fd, int attribute_num, int *value);
    int atomisp_set_zoom (int fd, int zoom);
    int xioctl(int fd, int request, void *arg) const;
    int v4l2_subscribe_event(int fd, int event);
    int v4l2_unsubscribe_event(int fd, int event);
    int v4l2_dqevent(int fd, struct v4l2_event *event);

    int startFileInject(void);
    int stopFileInject(void);
    status_t fileInjectSetSize(void);

    status_t selectCameraSensor();
    size_t setupCameraInfo();
    int getNumOfSkipFrames(void);
    int getPrimaryCameraIndex(void) const;
    status_t applySensorFlip(void);

// private members
private:

    const sp<CameraConf> mCameraConf;

    static cameraInfo sCamInfo[MAX_CAMERA_NODES];

    AtomMode mMode;
    Callbacks *mCallbacks;

    int mNumBuffers;
    int mNumPreviewBuffers;
    AtomBuffer *mPreviewBuffers;
    AtomBuffer *mRecordingBuffers;

    bool mNeedReset; /*!< TODO: remove, see BZ 72616 */

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
    ContinuousCaptureConfig mContCaptConfig;

    // TODO: video_fds should be moved to mDevices
    int video_fds[V4L2_MAX_DEVICE_COUNT];
    struct {
      unsigned int frameCounter;
      DeviceState state;
    } mDevices[V4L2_MAX_DEVICE_COUNT];

    int dumpPreviewFrame(int previewIndex);
    int dumpRecordingFrame(int recordingIndex);
    int dumpSnapshot(int snapshotIndex, int postviewIndex);
    int dumpRawImageFlush(void);
    bool isDumpRawImageReady(void);

    struct v4l2_buffer_pool v4l2_buf_pool[V4L2_MAX_DEVICE_COUNT]; //pool[0] for device0 pool[1] for device1

    bool mIsFileInject;
    struct FileInject {
        String8 fileName;
        bool active;
        unsigned int width;
        unsigned int height;
        unsigned int size;
        int stride;
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
    bool mFrameSyncRequested;
    bool mFrameSyncEnabled;
    v4l2_colorfx mColorEffect;

}; // class AtomISP

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_ISP
