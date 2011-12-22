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

#ifndef ANDROID_HARDWARE_CAMERA_HARDWARE_H
#define ANDROID_HARDWARE_CAMERA_HARDWARE_H

#include <semaphore.h>
#include <hardware/camera.h>
#include <camera/CameraParameters.h>
#include "IntelCamera.h"
#include "CameraAAAProcess.h"
#include <utils/threads.h>
#include <atomisp_config.h>
#include "JpegEncoder.h"

#include "libjpegwrap.h"

namespace android {

struct BCBuffer {
    camera_memory_t*  mem;

    int total_size;  // the heap size
    int src_size;   // psrc buffer size

    int jpeg_size;  // the encoded jpeg file size

    void *psrc;    // for YUV/RGB data, comes from ISP
    void *pdst_exif;  // for exif part
    void *pdst_thumbnail;   // for thumbnail
    void *pdst_main;    // for main part

    bool ready; // if the src data is ready, it is true
    bool encoded;   // if finished encoded, it is true
    int sequence;   // it is the handling sequence, initialization value is ~0

    // below variables only valid when the hardware jpeg buffer sharing is enabled
    void *usrptr;   // get from libva, it is allocated by libva. to store the src data for jpeg enc.
};

class CameraHardware {
public:
    CameraHardware(int cameraId);
    ~CameraHardware();

    status_t    setPreviewWindow(struct preview_stream_ops *window);

    void        setCallbacks(camera_notify_callback notify_cb,
                                     camera_data_callback data_cb,
                                     camera_data_timestamp_callback data_cb_timestamp,
                                     camera_request_memory get_memory,
                                     void* user);

    void        enableMsgType(int32_t msgType);
    void        disableMsgType(int32_t msgType);
    bool        msgTypeEnabled(int32_t msgType);

    status_t    startPreview();
    void        stopPreview();
    bool        previewEnabled();

    status_t    startRecording();
    void        stopRecording();
    bool        recordingEnabled();
    void        releaseRecordingFrame(const void* mem);

    status_t    autoFocus();
    status_t    cancelAutoFocus();

    status_t    touchToFocus(int blockNumber);
    status_t    cancelTouchToFocus();

    status_t    takePicture();
    status_t    cancelPicture();
    status_t    dump(int fd) const;
    status_t    setParameters(const char* params);
    status_t    setParameters(const CameraParameters& params);
    char*       getParameters() const;
    void        putParameters(char *params);
    status_t    sendCommand(int32_t command, int32_t arg1, int32_t arg2);
    static int  getNumberOfCameras();
    static int  getCameraInfo(int cameraId, struct camera_info* cameraInfo);
    status_t    storeMetaDataInBuffers(bool enable);
    void        release();

	static CameraHardware* createInstance(int cameraId);

    /* File input interfaces */
    status_t    setFileInputMode(int enable);
    status_t    configureFileInput(char *file_name, int width, int height,
                                   int format, int bayer_order);

private:
    static CameraHardware* singleton;

    class PreviewThread : public Thread {
        CameraHardware* mHardware;
    public:
        PreviewThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraPreviewThread", PRIORITY_URGENT_DISPLAY);
        }
        virtual bool threadLoop() {
            mHardware->previewThreadWrapper();
            // loop until we need to quit
            return false;
        }
    };

    class PictureThread : public Thread {
        CameraHardware* mHardware;
    public:
        PictureThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->pictureThread();
            return false;
        }
    };

    class AutoFocusThread : public Thread {
        CameraHardware* mHardware;
    public:
        AutoFocusThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual bool threadLoop() {
            mHardware->autoFocusThread();
            return false;
        }
    };

    class AeAfAwbThread : public Thread {
        CameraHardware* mHardware;
    public:
        AeAfAwbThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraAeAfAwbThread", PRIORITY_DEFAULT);
        }
        virtual bool threadLoop() {
            mHardware->aeAfAwbThread();
            return false;
        }
    };

    /* for burst capture, it will compress jpeg image in this thread */
    class CompressThread : public Thread {
        CameraHardware* mHardware;
    public:
        CompressThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual bool threadLoop()
        {
            return mHardware->compressThread();
        }
    };

    /* for DIS DVS, it will get DIS STAT data from ISP driver */
    class DvsThread : public Thread {
        CameraHardware* mHardware;
    public:
        DvsThread(CameraHardware* hw) :
            Thread(false),
            mHardware(hw) { }
        virtual void onFirstRef() {
            run("CameraDvsThread", PRIORITY_BACKGROUND);
        }
        virtual bool threadLoop()
        {
            return mHardware->dvsThread();
        }
    };

    void setSkipFrame(int frame);
    void setSkipSnapFrame(int frame);

    mutable Condition   mPreviewFrameCondition;
    bool        mExitAeAfAwbThread;
    mutable Mutex       mAeAfAwbLock;
    mutable Condition   mAeAfAwbEndCondition;

    // Used by preview thread to block
    mutable Mutex       mPreviewLock;
    mutable Condition   mPreviewCondition;
    bool        mPreviewRunning;
    bool        mExitPreviewThread;

    int setISPParameters(const CameraParameters&
            new_params, const CameraParameters &old_params);

    int update3AParameters(CameraParameters& p, bool flush_only);

    void initHeapLocked(int size);
    void initDefaultParameters();
    void initPreviewBuffer(int size);
    void deInitPreviewBuffer();
    bool checkRecording(int width, int height);
    void initRecordingBuffer(int size, int padded_size);
    void deInitRecordingBuffer();
    int  encodeToJpeg(int width, int height, void *psrc, void *pdst, int *jsize, int quality);
    void processPreviewFrame(void *buffer);
    void processRecordingFrame(void *buffer, int index);
    void print_snapshot_time();

    bool        mRecordRunning;

    mutable Mutex       mRecordLock;
    int recordingThread();
#if ENABLE_BUFFER_SHARE_MODE
    int getSharedBuffer();
    bool checkSharedBufferModeOff();
    bool requestEnableSharingMode();
    bool requestDisableSharingMode();
#endif
    mutable Mutex       mLock;

    CameraParameters    mParameters;

    bool mFlush3A;

    struct frame_buffer {
        camera_memory_t*    mem;
        void*               base[PREVIEW_NUM_BUFFERS];
        size_t              baseSize;
        uint8_t*            start[PREVIEW_NUM_BUFFERS];
        unsigned int        flags[PREVIEW_NUM_BUFFERS];
#if ENABLE_BUFFER_SHARE_MODE
        unsigned char*      pointerArray[PREVIEW_NUM_BUFFERS];
#endif
    } mPreviewBuffer, mRecordingBuffer;

    camera_memory_t*    mRawIdMem;
    camera_memory_t*    mUserptrMem[PREVIEW_NUM_BUFFERS];
    camera_memory_t*    mPreviewConvertMem;
    camera_memory_t*    mRecordConvertMem;

    preview_stream_ops_t*   mPreviewWindow;
    int                 mCameraId;
    int                 mPreviewFrame;
    int                 mPostPreviewFrame;

    int                 mRecordingFrame;
    int                 mPostRecordingFrame;

    unsigned int        mPreviewPixelFormat;
    unsigned int        mPicturePixelFormat;
	bool                mVideoPreviewEnabled;

    camera_memory_t*    mRawMem;

    IntelCamera*        mCamera;
    //sensor_info_t      *mCurrentSensor;

    int                 mPreviewFrameSize;
    int                 mRecorderFrameSize;

    // protected by mLock
    sp<PreviewThread>   mPreviewThread;
    int previewThread();
    int previewThreadWrapper();

    sp<AutoFocusThread> mAutoFocusThread;
    int         autoFocusThread();
    bool         mExitAutoFocusThread;

    sp<AeAfAwbThread> mAeAfAwbThread;
    int         aeAfAwbThread();
    bool        mPreviewAeAfAwbRunning;
    mutable Condition   mPreviewAeAfAwbCondition;

    sp<PictureThread>   mPictureThread;
    int         pictureThread();
    bool        mCaptureInProgress;
    static const int mJpegQualityDefault = 100; // default Jpeg Quality
    static const int mJpegThumbnailQualityDefault = 50; // default Jpeg thumbnail Quality

    /*for burst capture */
    sp<CompressThread> mCompressThread;
    int         compressThread();
    mutable Mutex       mCompressLock;
    mutable Condition   mCompressCondition;

    // for DIS DVS
    sp<DvsThread> mDvsThread;
    int         dvsThread();
    mutable Mutex       mDvsMutex;
    mutable Condition   mDvsCondition;
    bool mExitDvsThread;

    camera_notify_callback    mNotifyCb;
    camera_data_callback      mDataCb;
    camera_data_timestamp_callback mDataCbTimestamp;
    camera_request_memory mGetMemory;

    void               *mCallbackCookie;

    int32_t             mMsgEnabled;

    // fps
    nsecs_t             mPreviewLastTS;
    float               mPreviewLastFPS;
    nsecs_t             mRecordingLastTS;
    float               mRecordingLastFPS;
    int                 mSkipFrame; // skipped preview frames
    int                 mSkipSnapFrame; // skipped snapshot frames
    int                 mPreviewSkipFrame;
    int                 mSnapshotSkipFrame;

    //Postpreview
    int                 mPostViewWidth;
    int                 mPostViewHeight;
    int                 mPostViewSize;
    int                 mPostViewFormat;

    //3A
    AAAProcess          *mAAA;
    bool                awb_to_manual;

    //AE Weight Map
    ci_adv_weight_map   mAeWeightMap;
    int                 WeightTable[9];

    //File Input
    struct file_input   mFile;
#if ENABLE_BUFFER_SHARE_MODE
    bool                   isVideoStarted;
    bool                   isCameraTurnOffBufferSharingMode;
#endif

    int mSensorType;

    inline void setBF(unsigned int *bufferFlag, unsigned int flag) {
        *bufferFlag |= flag;
    }

    inline void clrBF(unsigned int *bufferFlag, unsigned int flag) {
        *bufferFlag &= ~flag;
    }

    inline bool isBFSet(unsigned int bufferFlag,unsigned int flag) {
        return (bufferFlag & flag);
    }

    enum {
        BF_ENABLED = 0x00000001,
        BF_LOCKED
    };
    //Used for picture taken time calculation
#ifdef PERFORMANCE_TUNING
    struct timeval  picture_start, preview_stop, pic_thread_start, snapshot_start, first_frame;
    struct timeval  second_frame, postview, snapshot_stop, jpeg_encoded, preview_start;
#endif

    // exif part
    static const unsigned exif_offset =  64*1024;  // must page size aligned, exif must less 64KBytes
    static const unsigned thumbnail_offset = 600*1024; // must page size aligned, max is 640*480*2
    void exifAttribute(exif_attribute_t& attribute, int cap_w, int cap_h, bool thumbnail_en, bool flash_en);
    void exifAttributeOrientation(exif_attribute_t& attribute);
    void exifAttributeGPS(exif_attribute_t& attribute);

    //still af
    int runStillAfSequence();
    static const int mStillAfMaxTimeMs = 1700;

    //flash
    bool mPreFlashSucceeded;
    void runPreFlashSequence (void);
    int SnapshotPostProcessing(void *img_data, int width, int height);
    void update3Aresults(void);
    int calculateLightLevel();
    bool mFlashNecessary;
    float mFramerate;

    void setupPlatformType(void);

    // snapshot internal functions
    int snapshotSkipFrames(void **main, void **postview);

    /*for burst capture */
    sem_t sem_bc_captured;  // +1 if it gets one frame from driver
    sem_t sem_bc_encoded;   // +1 if  it finishes encoding one frame
    void burstCaptureInit(bool init_flags);
    int burstCaptureAllocMem(int total_size, int rgb_frame_size,
                            int cap_w, int cap_h, int jpeg_buf_size, void *postview_out);
    void burstCaptureFreeMem(void);
    int burstCaptureSkipReqBufs(int i, int *idx, void **main, void **postview);
    int burstCaptureStart(void);
    void burstCaptureStop(void);
    int burstCaptureHandle(void);
    void burstCaptureCancelPic(void);
    bool mBCEn;   // true is enabled, false is disabled.
    bool mBCCancelCompress;  // cancelPicture has been called
    bool mBCCancelPicture;  // cancelPicture has been called
    int mBCNumReq; // remember the request capture number
    int mBCNumCur; // current the sequence capture number
    int mBCNumSkipReq; // need skipped number
    int mBCMemState; // true has been allocated, false has been released
    int mBCDeviceState; // true: device has been opened. false: device has been closed.
    HWLibjpegWrap *mBCLibJpgHw;
    camera_memory_t* mBCHeapHwJpgDst; // the hw jpeg wrapper's limitation. it outputs jpg data to this buffer only.
    void *mBCHwJpgDst; // point to mBCHeapHwJpgDst
    bool mHwJpegBufferShareEn;
    camera_memory_t* mBCHeap; // for store the structure BCBuffer
    struct BCBuffer *mBCBuffer; // point to mBCHeap
    int mManualFocusPosi;

    int mFlipMode;
    bool mCanFlip;
    static int num_cameras;

    void setFlip(void); /*set flip for sensor*/
    void resetFlip(void);   /*reset flip for sensor*/
};

}; // namespace android

#endif
