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

#ifdef ENABLE_INTEL_METABUFFER
#include "IntelMetadataBuffer.h"
#endif

#include "AtomAIQ.h"
#include "AtomAAA.h"
#include "PlatformData.h"
#include "CameraConf.h"
#include "I3AControls.h"
#include "AtomIspObserverManager.h"

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
    int cache_flags; /*!< initial flags used when creating buffers */
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
    bool fetched; // true if data has been attempted to read, false otherwise
};

class Callbacks;

class AtomISP : public I3AControls, public IBufferOwner {
// FIXME: Only needed for NVM parsing "cameranvm_create()" in AtomAAA
    friend class AtomAIQ;
    friend class AtomAAA;

// public types
public:
    enum ObserverType {
        OBSERVE_PREVIEW_STREAM,
        OBSERVE_FRAME_SYNC_SOF
    };

// constructor/destructor
public:
    explicit AtomISP(int cameraId);
    ~AtomISP();

    status_t initDevice();
    status_t init();
    void deInitDevice();
    bool isDeviceInitialized() const;

// prevent copy constructor and assignment operator
private:
    AtomISP(const AtomISP& other);
    AtomISP& operator=(const AtomISP& other);

    // public types
public:
    struct ContinuousCaptureConfig {
        int numCaptures;        /*!< Number of captures
                                 * -1 = capture continuously
                                 * 0 = disabled, stop captures
                                 * >0 = burst of N snapshots
                                 */
        int offset;             /*!< burst start offset */
        int skip;               /*!< skip factor */
    };

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
    AtomMode getMode() const { return mMode; };

    void requestClearDriverState();
    void clearDriverState();

    status_t startOfflineCapture(ContinuousCaptureConfig &config);
    status_t stopOfflineCapture();
    bool isOfflineCaptureRunning() const;
    bool isOfflineCaptureSupported() const;
    int shutterLagZeroAlign() const;
    int continuousBurstNegMinOffset(void) const;
    int continuousBurstNegOffset(int skip, int startIndex) const;
    int getContinuousCaptureNumber() const;
    status_t prepareOfflineCapture(ContinuousCaptureConfig &config, bool capturePriority);

    bool isYUVvideoZoomingSupported() const;
    status_t returnRecordingBuffers();
    bool isSharedPreviewBufferConfigured(bool *reserved = NULL) const;

    // TODO: client no longer using, can be moved to privates
    status_t getPreviewFrame(AtomBuffer *buff);
    status_t putPreviewFrame(AtomBuffer *buff);

    status_t setGraphicPreviewBuffers(const AtomBuffer *buffs, int numBuffs, bool cached);
    status_t getRecordingFrame(AtomBuffer *buff);
    status_t putRecordingFrame(AtomBuffer *buff);

    status_t setSnapshotBuffers(Vector<AtomBuffer> *buffs, int numBuffs, bool cached);
    status_t getSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf);
    status_t putSnapshot(AtomBuffer *snaphotBuf, AtomBuffer *postviewBuf);

    int pollPreview(int timeout);
    int pollCapture(int timeout);

    bool dataAvailable();
    bool isBufferValid(const AtomBuffer * buffer) const;

    status_t setPreviewFrameFormat(int width, int height, int format = 0);
    status_t setPostviewFrameFormat(int width, int height, int format);
    void getPostviewFrameFormat(int &width, int &height, int &format) const;
    status_t setSnapshotFrameFormat(int width, int height, int format);
    status_t setVideoFrameFormat(int width, int height, int format = 0);
    bool applyISPLimitations(CameraParameters *params, bool dvsEnabled, bool videoMode);

    void setPreviewFramerate(int fps);
    inline int getSnapshotPixelFormat() { return mConfig.snapshot.format; }
    void getVideoSize(int *width, int *height, int *stride);
    void getPreviewSize(int *width, int *height, int *stride);
    int getSnapshotNum();

    void getZoomRatios(bool videoMode, CameraParameters *params);
    void getFocusDistances(CameraParameters *params);
    status_t setZoom(int zoom);
    status_t setFlash(int numFrames);
    status_t setFlashIndicator(int intensity);
    status_t setTorch(int intensity);
    status_t setColorEffect(v4l2_colorfx effect);
    status_t applyColorEffect();
    status_t getMakerNote(atomisp_makernote_info *info);
    status_t getContrast(int *value);
    status_t setContrast(int value);
    status_t getSaturation(int *value);
    status_t setSaturation(int value);
    status_t getSharpness(int *value);
    status_t setSharpness(int value);
    status_t setXNR(bool enable);
    status_t setLowLight(bool enable);
    status_t setGDC(bool enable);
    bool getPreviewTooBigForVFPP() { return mPreviewTooBigForVFPP; }

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
    inline String8 getFileInjectionFileName(void) const { return mFileInject.fileName; }

    // camera hardware information
    static int getNumberOfCameras();
    static status_t getCameraInfo(int cameraId, camera_info *cameraInfo);
    status_t getSensorParams(SensorParams *sp);

    float getFrameRate() const { return mConfig.fps; }

    /* Acceleration API extensions */
    int loadAccFirmware(void *fw, size_t size, unsigned int *fwHandle);
    int loadAccPipeFirmware(void *fw, size_t size, unsigned int *fwHandle);
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
    int getCssMajorVersion();
    int getCssMinorVersion();
    int getIspHwMajorVersion();
    int getIspHwMinorVersion();
    /* Flash related controls */
    int setFlashIntensity(int intensity);
    /* file injection controls */
    void getSensorDataFromFile(const char *file_name, sensorPrivateData *sensor_data);

    // I3AControls
    virtual status_t init3A();
    virtual status_t deinit3A();
    virtual void getDefaultParams(CameraParameters *params, CameraParameters *intel_params);
    virtual status_t setAeMode(AeMode mode);
    virtual AeMode getAeMode();
    virtual status_t setEv(float bias);
    virtual status_t getEv(float *ret);
    virtual status_t setAeSceneMode(SceneMode mode);
    virtual SceneMode getAeSceneMode();
    virtual status_t setAwbMode(AwbMode mode);
    virtual AwbMode getAwbMode();
    virtual status_t setManualIso(int iso);
    virtual status_t getManualIso(int *ret);
    /** expose iso mode setting*/
    virtual status_t setIsoMode(IsoMode mode);
    virtual IsoMode getIsoMode(void);
    virtual status_t setAeMeteringMode(MeteringMode mode);
    virtual MeteringMode getAeMeteringMode();
    virtual status_t set3AColorEffect(const char *effect);
    virtual status_t setAeFlickerMode(FlickerMode flickerMode);
    virtual status_t setAfMode(AfMode mode);
    virtual AfMode getAfMode();
    virtual status_t setAfEnabled(bool en);
    int     get3ALock(); // helper method for 3A lock setters/getters
    virtual bool     getAeLock();
    virtual status_t setAeLock(bool en);
    virtual bool     getAfLock();
    virtual status_t setAfLock(bool en);
    virtual status_t setAwbLock(bool en);
    virtual bool     getAwbLock();
    virtual status_t getCurrentFocusPosition(int *pos);
    virtual status_t applyEv(float bias);
    virtual status_t setManualShutter(float expTime);
    virtual status_t setAeFlashMode(FlashMode mode);
    virtual FlashMode getAeFlashMode();
    virtual void setPublicAeMode(AeMode mode);
    virtual AeMode getPublicAeMode();
    virtual void setPublicAfMode(AfMode mode);
    virtual AfMode getPublicAfMode();

    // Only supported by Intel 3A
    virtual bool isIntel3A() { return false; }
    virtual status_t getAeManualBrightness(float *ret) { return INVALID_OPERATION; }
    virtual size_t   getAeMaxNumWindows() { return 0; }
    virtual size_t   getAfMaxNumWindows() { return 0; }
    virtual status_t setAeWindow(const CameraWindow *window) { return INVALID_OPERATION; }
    virtual status_t setAfWindows(const CameraWindow *windows, size_t numWindows) { return INVALID_OPERATION; }
    virtual status_t getAfLensPosRange(ia_3a_af_lens_range *lens_range) { return INVALID_OPERATION; }
    virtual status_t setManualFocusIncrement(int step) { return INVALID_OPERATION; }
    virtual status_t initAfBracketing(int stops,  AFBracketingMode mode) { return INVALID_OPERATION; }
    virtual status_t initAeBracketing() { return INVALID_OPERATION; }
    virtual status_t updateManualFocus() { return INVALID_OPERATION; }
    virtual status_t getExposureInfo(SensorAeConfig& sensorAeConfig) { return INVALID_OPERATION; }
    virtual status_t getGridWindow(AAAWindowInfo& window);
    virtual bool getAfNeedAssistLight() { return false; }
    virtual bool getAeFlashNecessary() { return false; }
    virtual ia_3a_awb_light_source getLightSource() { return ia_3a_awb_light_source_other; }
    virtual status_t setAeBacklightCorrection(bool en) { return INVALID_OPERATION; }
    virtual status_t setAwbMapping(ia_3a_awb_map mode) { return INVALID_OPERATION; }

    virtual status_t apply3AProcess(bool read_stats, struct timeval capture_timestamp, struct timeval sof_timestamp) { return INVALID_OPERATION; }
    virtual status_t startStillAf() { return INVALID_OPERATION; }
    virtual status_t stopStillAf() { return INVALID_OPERATION; }
    virtual ia_3a_af_status isStillAfComplete() { return ia_3a_af_status_error; }
    virtual status_t applyPreFlashProcess(FlashStage stage) { return INVALID_OPERATION; }

    virtual ia_3a_mknote *get3aMakerNote(ia_3a_mknote_mode mode) { return NULL; }
    virtual void put3aMakerNote(ia_3a_mknote *mknData) { }
    virtual void reset3aMakerNote(void) { }
    virtual int add3aMakerNoteRecord(ia_3a_mknote_field_type mkn_format_id,
                                     ia_3a_mknote_field_name mkn_name_id,
                                     const void *record,
                                     unsigned short record_size) { return -1; }

    virtual status_t setSmartSceneDetection(bool en) { return INVALID_OPERATION; }
    virtual bool     getSmartSceneDetection() { return false; }
    virtual status_t switchModeAndRate(AtomMode mode, float fps) { return INVALID_OPERATION; }

    virtual int dumpCurrent3aStatToFile(void) { return -1; }
    virtual int init3aStatDump(const char * str_mode) { return INVALID_OPERATION; }
    virtual int deinit3aStatDump(void) { return INVALID_OPERATION; }

    virtual ia_3a_af_status getCAFStatus() { return ia_3a_af_status_error; }
    status_t getSmartSceneMode(int *sceneMode, bool *sceneHdr) { return INVALID_OPERATION; }
    status_t setFaces(const ia_face_state& faceState) { return INVALID_OPERATION; }

    // AtomIspObserver controls
    status_t attachObserver(IAtomIspObserver *observer, ObserverType t);
    status_t detachObserver(IAtomIspObserver *observer, ObserverType t);
    void pauseObserver(ObserverType t);

    // IBufferOwner override
    virtual void returnBuffer(AtomBuffer* buff);

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
        float fps;            // preview/recording (shared) output by sensor
        int target_fps ;      // preview/recording requested by user
        int num_snapshot;     // number of snapshots to take
        int num_postviews;    // number of allocated postviews
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
        RESOLUTION_1_3MP,
        RESOLUTION_2MP,
        RESOLUTION_1080P,
        RESOLUTION_3MP,
        RESOLUTION_5MP,
        RESOLUTION_8MP,
        RESOLUTION_13MP,
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

    status_t configurePreview();
    status_t startPreview();
    status_t stopPreview();
    status_t configureRecording();
    status_t startRecording();
    status_t stopRecording();
    status_t configureCapture();
    status_t configureContinuousMode(bool enable);
    status_t configureContinuousRingBuffer();
    status_t configureContinuous();
    status_t startCapture();
    status_t stopCapture();
    status_t stopContinuousPreview();

    status_t requestContCapture(int numCaptures, int offset, unsigned int skip);

    void runStartISPActions();
    void runStopISPActions();

    void markBufferCached(struct v4l2_buffer_info *vinfo, bool cached);

    status_t allocatePreviewBuffers();
    status_t allocateRecordingBuffers();
    status_t allocateSnapshotBuffers();
    status_t allocateMetaDataBuffers();
    status_t freePreviewBuffers();
    status_t freeRecordingBuffers();
    status_t freeSnapshotBuffers();
    status_t freePostviewBuffers();
    bool needNewPostviewBuffers();

#ifdef ENABLE_INTEL_METABUFFER
    void initMetaDataBuf(IntelMetadataBuffer* metaDatabuf);
#endif

    void getMaxSnapShotSize(int cameraId, int* width, int* height);

    status_t updateLowLight();
    status_t setTorchHelper(int intensity);
    status_t updateCaptureParams();

    int  openDevice(int device);
    void closeDevice(int device);
    int v4l2_poll(int device, int timeout);
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
    unsigned int getNumOfSkipFrames(void);
    int getPrimaryCameraIndex(void) const;
    status_t applySensorFlip(void);
    void fetchIspVersions();

private:
    // AtomIspObserver
    IObserverSubject* observerSubjectByType(ObserverType t);

    // Observer subject sub-classes
    class PreviewStreamSource: public IObserverSubject
    {
    public:
        PreviewStreamSource(const char*name, AtomISP *aisp)
            :mName(name), mISP(aisp) { };

        // IObserverSubject override
        virtual const char* getName() { return mName.string(); };
        virtual status_t observe(IAtomIspObserver::Message *msg);
        virtual bool checkSkipFrame(int frameNum);

    private:
        String8  mName;
        AtomISP *mISP;
    } mPreviewStreamSource;

    class FrameSyncSource: public IObserverSubject
    {
    public:
        FrameSyncSource(const char*name, AtomISP *aisp)
            :mName(name), mISP(aisp) { };

        // IObserverSubject override
        virtual const char* getName() { return mName.string(); };
        virtual status_t observe(IAtomIspObserver::Message *msg);

    private:
        String8  mName;
        AtomISP *mISP;
    } mFrameSyncSource;

// private members
private:

    int mCameraId;

    static cameraInfo sCamInfo[MAX_CAMERA_NODES];

    AtomMode mMode;
    Callbacks *mCallbacks;

    int mNumBuffers;
    int mNumPreviewBuffers;
    AtomBuffer *mPreviewBuffers;
    bool mPreviewBuffersCached;
    AtomBuffer *mRecordingBuffers;
    bool mSwapRecordingDevice;
    bool mRecordingDeviceSwapped;
    bool mPreviewTooBigForVFPP;

    bool mClientSnapshotBuffersCached;
    bool mUsingClientSnapshotBuffers;
    bool mStoreMetaDataInBuffers;

    AtomBuffer mSnapshotBuffers[MAX_BURST_BUFFERS];
    Vector <AtomBuffer> mPostviewBuffers;
    int mNumPreviewBuffersQueued;
    int mNumRecordingBuffersQueued;
    int mNumCapturegBuffersQueued;
    int mFlashTorchSetting;
    Config mConfig;
    ContinuousCaptureConfig mContCaptConfig;
    bool mContCaptPrepared;
    bool mContCaptPriority;
    unsigned int mInitialSkips;

    // TODO: video_fds should be moved to mDevices
    int video_fds[V4L2_MAX_DEVICE_COUNT];
    struct {
      unsigned int frameCounter;
      DeviceState state;
      Mutex       mutex;
      unsigned int initialSkips;
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
    struct cameraInfo *mCameraInput;

    bool mLowLight;
    int mXnr;

    char *mZoomRatios;

    int mRawDataDumpSize;
    int mFrameSyncRequested;
    bool mFrameSyncEnabled;
    v4l2_colorfx mColorEffect;

    AtomIspObserverManager mObserverManager;

    AeMode mPublicAeMode;
    AfMode mPublicAfMode;

    int mCssMajorVersion;
    int mCssMinorVersion;
    int mIspHwMajorVersion;
    int mIspHwMinorVersion;
}; // class AtomISP

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_ISP
