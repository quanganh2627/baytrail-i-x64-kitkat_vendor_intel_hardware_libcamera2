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

#define LOG_TAG "CameraHardware"
#include <utils/Log.h>

#include "CameraHardware.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "SkBitmap.h"
#include "SkImageEncoder.h"
#include "SkStream.h"

#if ENABLE_BUFFER_SHARE_MODE
#include <IntelBufferSharing.h>
#endif

#define MAX_CAMERAS 2		// Follow CamreaService.h
#define SENSOR_TYPE_SOC 0
#define SENSOR_TYPE_RAW 1


namespace android {

#define CAMERA_MSG_TOUCH_TO_FOCUS 0x200

bool share_buffer_caps_set=false;
static const int INITIAL_SKIP_FRAME = 1;
static const int CAPTURE_SKIP_FRAME = 1;

static inline long calc_timediff(struct timeval *t0, struct timeval *t1)
{
    return ((t1->tv_sec - t0->tv_sec) * 1000000 + t1->tv_usec - t0->tv_usec) / 1000;
}

CameraHardware::CameraHardware(int cameraId)
    :
    mCameraId(cameraId),
    mPreviewFrame(0),
    mPostPreviewFrame(0),
    mRecordingFrame(0),
    mPostRecordingFrame(0),
    mCamera(0),
    mPreviewFrameSize(0),
    mRecorderFrameSize(0),
    mCaptureInProgress(false),
    mNotifyCb(0),
    mDataCb(0),
    mDataCbTimestamp(0)
{
    int ret;
    LOG2("%s: Create the CameraHardware\n", __func__);
    mCamera = IntelCamera::createInstance();

    if (mCamera == NULL) {
        LOGE("ERR(%s):Fail on mCamera object creation", __func__);
    }

    if (use_texture_streaming && !memory_userptr) {
        LOGE("ERR(%s):texture streaming set but user pointer unset", __func__);
    }

    ret = mCamera->initCamera(cameraId);
    if (ret < 0) {
        LOGE("ERR(%s):Fail on mCamera init", __func__);
    }

    initDefaultParameters();
    mPicturePixelFormat = V4L2_PIX_FMT_RGB565;
    mVideoPreviewEnabled = false;

    mExitAutoFocusThread = false;
    mExitPreviewThread = false;
    mExitAeAfAwbThread = false;
    mPreviewRunning = false;
    mPreviewAeAfAwbRunning = false;
    mRecordRunning = false;
    mPreviewThread = new PreviewThread(this);
    mAutoFocusThread = new AutoFocusThread(this);
    mPictureThread = new PictureThread(this);
    mAeAfAwbThread = new AeAfAwbThread(this);

    mAAA = mCamera->getmAAA();

#if ENABLE_BUFFER_SHARE_MODE
    isRecordingStarted = false;
    isCameraTurnOffBufferSharingMode = false;
#endif
    LOGD("libcamera version: 2011-04-01 1.0.0");
}

CameraHardware::~CameraHardware()
{
    LOGI("%s: Delete the CameraHardware\n", __func__);

    if (mPreviewBuffer.heap != NULL) {
        mPreviewBuffer.heap->dispose();
        mPreviewBuffer.heap.clear();
    }
    if (mRecordingBuffer.heap != NULL) {
        mRecordingBuffer.heap->dispose();
        mRecordingBuffer.heap.clear();
    }

    if (mRawHeap != NULL) {
        mRawHeap->dispose();
        mRawHeap.clear();
    }

    mCamera->deinitCamera();
    mCamera = NULL;
    singleton.clear();
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;

    p.setPreviewSize(640, 480);
    if (use_texture_streaming)
        p.setPreviewFrameRate(30);
    else
        p.setPreviewFrameRate(15);
    p.setPreviewFormat("rgb565");

    p.setPictureFormat("jpeg");
    p.set("preview-format-values", "yuv420sp,rgb565");
    p.set("preview-size-values", "640x480");
    p.set("picture-format-values", "jpeg");
    p.set("jpeg-quality","100");

    p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, "auto,portrait,sports,landscape,night,fireworks");
    p.set(CameraParameters::KEY_SCENE_MODE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, "auto,incandescent,fluorescent,daylight,cloudy-daylight");
    p.set(CameraParameters::KEY_WHITE_BALANCE, "auto");

    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto,infinity,macro,continuous-video");
    p.set(CameraParameters::KEY_FOCUS_MODE, "auto");

    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "2");
    p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-2");
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "1");

    p.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, "auto,50hz,60hz,off");
    p.set(CameraParameters::KEY_ANTIBANDING, "auto");

    p.set("effect", "none");
    p.set("flash-mode","off");
    p.set("jpeg-quality-values","1,20,30,40,50,60,70,80,90,99,100");
    p.set("effect-values","none,mono,negative,sepia");
    p.set("flash-mode-values","off,auto,on");
    p.set("rotation-values","0,90,180");
    p.set("video-frame-format","yuv420sp");
    p.set("zoom-supported", "true");
    p.set("max-zoom", "64");
    p.set("zoom-ratios", "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,"
          "22,23,24,24,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,"
          "45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64");
    p.set("zoom", 0);
    p.set("focus-mode-values", "auto,infinity,macro");
    p.set("flash-mode-values","off,auto,on,torch");
    p.set("scene-mode-values","auto,action,portrait,landscape,night,night-portrait,theatre,beach,snow,sunset,steadyphoto,fireworks,party,candlelight");
    p.set("scene-mode","auto");
    p.set("setaeexposure-mode","auto");
    p.set("setaeexposure-mode-values","auto,portrait,sports,landscape,night,fireworks");
    p.set("aelock-mode","lock");
    p.set("aelock-mode-values","lock,unlock");
    p.set("backlightingcorrection-mode","on");
    p.set("backlightingcorrection-mode-values","on,off");
    p.set("af-mode","auto");
    p.set("af-mode-values","auto,manual");
    p.set("af-range","normal");
    p.set("af-range-values","normal,macro,full");
    p.set("afmetering-mode","auto");
    p.set("afmetering-mode-values","auto,spot");
    p.set("redeye-mode","on");
    p.set("redeye-mode-values","on,off");
    p.set("exposure-mode","ae");
    p.set("exposure-mode-values","ae,manual");
    p.set("exposure-compensation","1");
    p.set("max-exposure-compensation","5");
    p.set("min-exposure-compensation","0");
    p.set("exposure-compensation-step","0.5");
    p.set("xnr", "false");
    p.set("xnr-values", "true,false");
    p.set("digital-image-stablization", "off");
    p.set("digital-image-stablization-values", "on,off");
    p.set("temporal-noise-reduction", "off");
    p.set("temporal-noise-reduction-values", "on,off");
    p.set("noise-reduction-and-edge-enhancement", "off");
    p.set("noise-reduction-and-edge-enhancement-values", "on,off");
    p.set("multi-access-color-correction", "enhance-sky");
    p.set("multi-access-color-correction-values", "enhance-sky,enhance-grass,enhance-skin");
    p.set("ae-mode", "auto");
    p.set("ae-mode-values", "auto,manual,shutter-priority,aperture-priority");
    p.set("ae-metering-mode", "auto");
    p.set("ae-metering-mode-values", "auto,spot,center,customized");
    p.set("flicker-mode", "auto-select");
    p.set("flicker-mode-values", "50hz,60hz,auto-select");
    p.set("shutter", "60");
    p.set("shutter-values", "2s,1s,4,15,60,250");
    p.set("aperture", "f2.8");
    p.set("aperture-values", "f2.8,f4,f5.6,f8,f11,f16");
    p.set("iso", "iso-200");
    p.set("iso-values", "iso-100,iso-200,iso-400,iso-800");

    if (mCameraId == CAMERA_FACING_BACK) {
        // For main back camera
        p.set("rotation","90");
        p.set("picture-size-values","320x240,640x480,1024x768,1280x720,"
              "1920x1080,2048x1536,2560x1920,3264x2448,3840x2400,4096x3072,4352x3264");
    } else {
        // For front camera
        p.set("rotation","0");
        p.set("picture-size-values","320x240,640x480,1280x720,1920x1080");
    }

    mParameters = p;

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }
}

void CameraHardware::initPreviewBuffer(int size)
{
    unsigned int page_size = getpagesize();
    unsigned int size_aligned = (size + page_size - 1) & ~(page_size - 1);

    if (size != mPreviewFrameSize) {
        if (mPreviewBuffer.heap != NULL)
            deInitPreviewBuffer();
        mPreviewBuffer.heap = new MemoryHeapBase(size_aligned * kBufferCount);
        mRawHeap = new MemoryHeapBase(size_aligned);
        mFrameIdHeap = new MemoryHeapBase(sizeof(int));
        mFrameIdBase = new MemoryBase(mFrameIdHeap, 0, sizeof(int));

        for (int i = 0; i < kBufferCount; i++) {
            mPreviewBuffer.flags[i] = 0;
            mPreviewBuffer.base[i] =
                new MemoryBase(mPreviewBuffer.heap, i * size_aligned, size);
            mPreviewBuffer.start[i] = (uint8_t *)mPreviewBuffer.heap->base() +
                                      (i * size_aligned);
            LOG2("mPreviewBuffer.start[%d] = %p", i, mPreviewBuffer.start[i]);
            clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        }
        LOG1("PreviewBufferInfo: num(%d), size(%d), heapsize(%d)\n",
             kBufferCount, size, mPreviewBuffer.heap->getSize());
        mPreviewFrameSize = size;
    }

    if (memory_userptr)
        for (int i = 0; i < kBufferCount; i++)
            mCamera->setPreviewUserptr(i, mPreviewBuffer.start[i]);
}

void CameraHardware::deInitPreviewBuffer()
{
    for (int i=0; i < kBufferCount; i++)
        mPreviewBuffer.base[i].clear();
    mPreviewBuffer.heap.clear();
    mFrameIdBase.clear();
    mFrameIdHeap.clear();
}

/*
    This function will check the recording width and height
    it will return true, if we the recording resolution is specialled.
    Example, 720p and 1080p. For the video bianry couldn't ouput the same size picture
    in the video0 and video1.
*/
bool CameraHardware::checkRecording(int width, int height)
{
#define W_720P  1280
#define H_720P  720
#define W_1080P 1920
#define H_1080P 1080
        if ((W_720P == width) && (H_720P == height))
            return true;
        if ((W_1080P == width) && (H_1080P == height))
            return true;

        return false;
}
void CameraHardware::initRecordingBuffer(int size)
{
    //Init the preview stream buffer first
    int w, h, preview_size;
    unsigned int page_size = getpagesize();
    unsigned int size_aligned = (size + page_size - 1) & ~(page_size - 1);
    mPreviewFrame = 0;
    mPostPreviewFrame = 0;
    mCamera->getPreviewSize(&w, &h, &preview_size);
    initPreviewBuffer(preview_size);

    //Init the video stream buffer
    if (mRecordingBuffer.heap != NULL)
        deInitRecordingBuffer();

    mRecordingBuffer.heap = new MemoryHeapBase(size_aligned * kBufferCount);
    for (int i = 0; i < kBufferCount; i++) {
        mRecordingBuffer.flags[i] = 0;
        mRecordingBuffer.base[i] = new MemoryBase(mRecordingBuffer.heap,
                i * size_aligned, size);
        mRecordingBuffer.start[i] = (uint8_t *)mRecordingBuffer.heap->base()
                                    + (i * size_aligned);
        clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        LOG1("RecordingBufferInfo: num(%d), size(%d), heapsize(%d)\n",
             kBufferCount, size, mRecordingBuffer.heap->getSize());

    }
    mRecorderFrameSize = size;

    if (memory_userptr)
        for (int i = 0; i < kBufferCount; i++)
            mCamera->setRecorderUserptr(i, mPreviewBuffer.start[i],
                                      mRecordingBuffer.start[i]);
}

void CameraHardware::deInitRecordingBuffer()
{
    if (mRecordingBuffer.heap != NULL) {
        for (int i = 0; i < kBufferCount; i++)
            mRecordingBuffer.base[i].clear();
        mRecordingBuffer.heap->dispose();
        mRecordingBuffer.heap.clear();
    }
}

sp<IMemoryHeap> CameraHardware::getPreviewHeap() const
{
    return mPreviewBuffer.heap;
}

sp<IMemoryHeap> CameraHardware::getRawHeap() const
{
    return mRawHeap;
}

void CameraHardware::setCallbacks(notify_callback notify_cb,
                                  data_callback data_cb,
                                  data_callback_timestamp data_cb_timestamp,
                                  void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void CameraHardware::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void CameraHardware::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool CameraHardware::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

void CameraHardware::setSkipFrame(int frame)
{
    Mutex::Autolock lock(mSkipFrameLock);
    mSkipFrame = frame;
}

void CameraHardware::processPreviewFrame(void *buffer)
{
    //Copy the preview frame out.
    LOG2("%s: Begin processPreviewFrame, buffer=%p\n", __func__, buffer);
    int previewFrame = mPreviewFrame;
    if (!isBFSet(mPreviewBuffer.flags[previewFrame], BF_ENABLED) &&
        !isBFSet(mPreviewBuffer.flags[previewFrame], BF_LOCKED)) {
        if (!use_texture_streaming) {
            setBF(&mPreviewBuffer.flags[previewFrame], BF_LOCKED);
            memcpy(mPreviewBuffer.start[previewFrame], buffer, mPreviewFrameSize);
            clrBF(&mPreviewBuffer.flags[previewFrame],BF_LOCKED);
        }
        setBF(&mPreviewBuffer.flags[previewFrame],BF_ENABLED);
        mPreviewFrame = (previewFrame + 1) % kBufferCount;
    }
    // Notify the client of a new preview frame.
    int postPreviewFrame = mPostPreviewFrame;
    if (isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_ENABLED) &&
        !isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_LOCKED)) {
            ssize_t offset;
            size_t size;
            mPreviewBuffer.base[postPreviewFrame]->getMemory(&offset, &size);
            //If we delete the LOGV here, the preview is black
            LOG2("%s: Postpreviwbuffer offset(%u), size(%u)\n", __func__,
                 (int)offset, (int)size);
        if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
            if (use_texture_streaming) {
                memcpy(mFrameIdHeap->base(), &postPreviewFrame, sizeof(int));
                mDataCb(CAMERA_MSG_PREVIEW_FRAME, mFrameIdBase, mCallbackCookie);
                LOG2("%s: send frame id: %d", __func__, postPreviewFrame);
            } else {
                mDataCb(CAMERA_MSG_PREVIEW_FRAME,
                        mPreviewBuffer.base[postPreviewFrame], mCallbackCookie);
            }
        }
        clrBF(&mPreviewBuffer.flags[postPreviewFrame],BF_LOCKED|BF_ENABLED);
    }
    mPostPreviewFrame = (postPreviewFrame + 1) % kBufferCount;
}

void CameraHardware::processRecordingFrame(void *buffer)
{
    if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
        //Copy buffer out from driver
        int recordingFrame = mRecordingFrame;

#if ENABLE_BUFFER_SHARE_MODE
    if(NO_ERROR != getSharedBuffer()) {
        return;
    }
    if(checkSharedBufferModeOff()) {
        return;
    }
#endif

        if (!isBFSet(mRecordingBuffer.flags[recordingFrame], BF_ENABLED) &&
            !isBFSet(mRecordingBuffer.flags[recordingFrame], BF_LOCKED)) {
            setBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
#if ENABLE_BUFFER_SHARE_MODE
            memcpy(mRecordingBuffer.pointerArray[recordingFrame], buffer,
                   mRecorderFrameSize);
#else
            memcpy(mRecordingBuffer.start[recordingFrame], buffer,
                   mRecorderFrameSize);
#endif
            clrBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
            setBF(&mRecordingBuffer.flags[recordingFrame],BF_ENABLED);
        }
        mRecordingFrame = (recordingFrame + 1) % kBufferCount;

        //Notify the client of a new recording frame.
        int postRecordingFrame = mPostRecordingFrame;
        if (!isBFSet(mRecordingBuffer.flags[postRecordingFrame], BF_LOCKED) &&
            isBFSet(mRecordingBuffer.flags[postRecordingFrame], BF_ENABLED)) {
            nsecs_t timestamp = systemTime(SYSTEM_TIME_MONOTONIC);
            clrBF(&mRecordingBuffer.flags[postRecordingFrame],BF_ENABLED);
            setBF(&mRecordingBuffer.flags[postRecordingFrame],BF_LOCKED);
            ssize_t offset;
            size_t size;
            mRecordingBuffer.base[postRecordingFrame]->getMemory(&offset, &size);
            LOGV("%s: Post Recording Buffer offset(%d), size(%d)\n", __func__,
                 (int)offset, (int)size);
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME,
                         mRecordingBuffer.base[postRecordingFrame], mCallbackCookie);
            mPostRecordingFrame = (postRecordingFrame + 1) % kBufferCount;
            LOGV("Sending the recording frame, size %d, index %d/%d\n",
                 mRecorderFrameSize, postRecordingFrame, kBufferCount);
        }
    }
}

// ---------------------------------------------------------------------------
int CameraHardware::previewThread()
{
    void *data;
    //DQBUF
    mDeviceLock.lock();
    int index = mCamera->getPreview(&data);
    mDeviceLock.unlock();

    if (index < 0) {
        LOGE("ERR(%s):Fail on mCamera->getPreview()", __func__);
        return -1;
    }

    //Run 3A after each frame
    mPreviewFrameCondition.signal();

    //Skip the first 3 frames from the sensor
    mSkipFrameLock.lock();
    if (mSkipFrame > 0) {
        mSkipFrame--;
        mSkipFrameLock.unlock();
        mCamera->putPreview(index);
        return NO_ERROR;
    }
    mSkipFrameLock.unlock();
    processPreviewFrame(data);

    //Qbuf
    mCamera->putPreview(index);

    return NO_ERROR;
}

int CameraHardware::recordingThread()
{
    void *main_out, *preview_out;
    mDeviceLock.lock();
    int index = mCamera->getRecording(&main_out, &preview_out);
    mDeviceLock.unlock();
    if (index < 0) {
        LOGE("ERR(%s):Fail on mCamera->getRecording()", __func__);
        return -1;
    }
    //Run 3A after each frame
    mPreviewFrameCondition.signal();
    //Process the preview frame first
    processPreviewFrame(preview_out);
    //Process the recording frame when recording started
    if (mRecordRunning)
        processRecordingFrame(main_out);
    mCamera->putRecording(index);
    return NO_ERROR;
}

int CameraHardware::previewThreadWrapper()
{
    int vf_mode;
    while (1) {
        mPreviewLock.lock();
        while (!mPreviewRunning) {
            LOGI("%s: preview is waiting", __func__);
            //do the stop here. Delay for the race condition with stopPreview
            mPreviewCondition.wait(mPreviewLock);
            LOGI("%s: preview return from wait", __func__);
        }
        mPreviewLock.unlock();

        if (mExitPreviewThread) {
            LOGI("%s: preview exiting", __func__);
            return 0;
        }

        if(mVideoPreviewEnabled) {
            //for video capture preview
            if (recordingThread() < 0) {
                mCamera->stopCameraRecording();
                mPreviewLock.lock();
                mPreviewRunning = false;
                mExitPreviewThread = true;
                mPreviewLock.unlock();
                return -1;
            }
        } else {
            //For normal preview
            if (previewThread() < 0) {
                mCamera->stopCameraPreview();
                mPreviewLock.lock();
                mPreviewRunning = false;
                mExitPreviewThread = true;
                mPreviewLock.unlock();
                LOGI("%s: preview thread exit from error", __func__);
                return -1;
            }
        }
        // next frame loop
    }
}

int CameraHardware::aeAfAwbThread()
{
    while (1) {
        if (mExitAeAfAwbThread) {
            LOGD("%s Exiting the 3A thread\n", __func__);
            return 0;
        }

        mAeAfAwbLock.lock();
        while (!mPreviewAeAfAwbRunning) {
            LOGI("%s: previewaeafawb is waiting", __func__);
            //Tell stop preview to continue
            mAeAfAwbEndCondition.signal();
            mPreviewAeAfAwbCondition.wait(mAeAfAwbLock);
            LOGI("%s: previewaeafawb return from wait", __func__);
        }
        mAeAfAwbLock.unlock();
        //Check exit. Maybe we are waken up from the release. We don't go to
        //sleep again.
        if (mExitAeAfAwbThread) {
            LOGD("%s Exiting the 3A thread\n", __func__);
            return 0;
        }

        mPreviewFrameLock.lock();
        mPreviewFrameCondition.wait(mPreviewFrameLock);
        LOG2("%s: 3A return from wait", __func__);
        mPreviewFrameLock.unlock();
        mCamera->runAeAfAwb();
        LOG2("%s: After run 3A thread", __func__);
    }
}

//This function need optimization. FIXME
//We don't need to allocate the video buffers as it is not used in the
//snapshot
void CameraHardware::initHeapLocked(int preview_size)
{
}

void CameraHardware::print_snapshot_time(void)
{
#ifdef PERFORMANCE_TUNING
    LOGD("stop preview: %ldms\n", calc_timediff(&picture_start, &preview_stop));
    LOGD("start picture thead %ldms\n", calc_timediff(&preview_stop, &pic_thread_start));
    LOGD("snapshot start %ldms\n", calc_timediff(&pic_thread_start, &snapshot_start));
    LOGD("take first frame %ldms\n", calc_timediff(&pic_thread_start, &first_frame));
    LOGD("take second frame %ldms\n", calc_timediff(&first_frame, &second_frame));
    LOGD("Postview %ldms\n", calc_timediff(&second_frame, &postview));
    LOGD("snapshot stop %ldms\n", calc_timediff(&postview, &snapshot_stop));
    LOGD("Jpeg encoded %ldms\n", calc_timediff(&snapshot_stop, &jpeg_encoded));
    LOGD("start preview %ldms\n", calc_timediff(&jpeg_encoded, &preview_start));
#endif
}

status_t CameraHardware::startPreview()
{
    int ret;
#ifdef PERFORMANCE_TUNING
    gettimeofday(&preview_start, 0);
    print_snapshot_time();
#endif
    Mutex::Autolock lock(mStateLock);
    if (mCaptureInProgress) {
        LOGE("ERR(%s) : capture in progress, not allowed", __func__);
        return INVALID_OPERATION;
    }

    mPreviewLock.lock();
    if (mPreviewRunning) {
        LOGE("ERR(%s) : preview thread already running", __func__);
        mPreviewLock.unlock();
        return INVALID_OPERATION;
    }

    if (mExitPreviewThread) {
        LOGE("ERR(%s) : preview thread is not exist", __func__);
        mPreviewLock.unlock();
        return INVALID_OPERATION;
    }
    setSkipFrame(INITIAL_SKIP_FRAME);

    //Enable the preview 3A
    mAeAfAwbLock.lock();
    mPreviewAeAfAwbRunning = true;
    mAeAfAwbLock.unlock();
    mPreviewAeAfAwbCondition.signal();

    //Determine which preview we are in
    if (mVideoPreviewEnabled) {
        int w, h, size;
        LOGD("Start recording preview\n");
        mRecordingFrame = 0;
        mPostRecordingFrame = 0;
        mCamera->getRecorderSize(&w, &h, &size);
        initRecordingBuffer(size);
        ret = mCamera->startCameraRecording();
    } else {
        LOGD("Start normal preview\n");
        int w, h, size;
        mPreviewFrame = 0;
        mPostPreviewFrame = 0;
        mCamera->getPreviewSize(&w, &h, &size);
        initPreviewBuffer(size);
        ret = mCamera->startCameraPreview();
    }
    if (ret < 0) {
        mPreviewRunning = false;
        mPreviewLock.unlock();
        mPreviewCondition.signal();
        LOGE("ERR(%s):Fail on mCamera->startPreview()", __func__);
        return -1;
    }

	flushParameters(mParameters);
    mPreviewRunning = true;
    mPreviewLock.unlock();
    mPreviewCondition.signal();

    return NO_ERROR;
}

void CameraHardware::stopPreview()
{
    LOG1("%s :", __func__);
    // request that the preview thread stop.
    if (!mPreviewRunning) {
        LOGI("%s : preview not running, doing nothing", __func__);
        return ;
    }
    //Waiting for the 3A to stop if it is running
    mAeAfAwbLock.lock();
    if (mPreviewAeAfAwbRunning) {
        mPreviewAeAfAwbRunning = false;
        mAeAfAwbEndCondition.wait(mAeAfAwbLock);
    }
    mAeAfAwbLock.unlock();

    //Tell preview to stop
    mPreviewRunning = false;
    LOGE("Stop the 3A now\n");

    //Waiting for DQ to finish
    usleep(100);
    mDeviceLock.lock();
    if(mVideoPreviewEnabled) {
        mCamera->stopCameraRecording();
        deInitRecordingBuffer();
    } else {
        mCamera->stopCameraPreview();
    }
    mDeviceLock.unlock();
}

bool CameraHardware::previewEnabled()
{
    Mutex::Autolock lock(mPreviewLock);
    return mPreviewRunning;
}

#if ENABLE_BUFFER_SHARE_MODE
int CameraHardware::getSharedBuffer()
{
   /* block until get the share buffer information*/
   if (!isRecordingStarted && mRecordRunning && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
       int bufferCount;
       unsigned char *pointer;
       SharedBufferType *pSharedBufferInfoArray = NULL;
       android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());

       LOGE("camera try to get share buffer array information");
       r->sourceEnterSharingMode();
       r->sourceGetSharedBuffer(NULL, &bufferCount);

       pSharedBufferInfoArray = (SharedBufferType *)malloc(sizeof(SharedBufferType) * bufferCount);
       if(!pSharedBufferInfoArray) {
           LOGE(" pShareBufferInfoArray malloc failed! ");
           return -1;
       }

       r->sourceGetSharedBuffer(pSharedBufferInfoArray, &bufferCount);
       LOGE("camera have already gotten share buffer array information");

       if(bufferCount > kBufferCount) {
           bufferCount = kBufferCount;
       }

       for(int i = 0; i < bufferCount; i ++)
       {
          mRecordingBuffer.pointerArray[i] = pSharedBufferInfoArray[i].pointer;
          *(unsigned char**)(mRecordingBuffer.start[i])= pSharedBufferInfoArray[i].pointer;
          LOGE("pointer[%d] = %p (%dx%d - stride %d) ",
               i,
               mRecordingBuffer.start[i],
               pSharedBufferInfoArray[i].width,
               pSharedBufferInfoArray[i].height,
               pSharedBufferInfoArray[i].stride);
       }
       delete [] pSharedBufferInfoArray;

       isRecordingStarted = true;
    }

    return NO_ERROR;
}

bool CameraHardware::checkSharedBufferModeOff()
{
   /* check whether encoder have send signal to stop buffer sharing mode.*/
   if(isCameraTurnOffBufferSharingMode){
       LOGE("isCameraTurnOffBufferSharingMode == true");
       return true;
    }

    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());

    if(!isCameraTurnOffBufferSharingMode
        && false == r->isBufferSharingModeSet()){
      LOGE("buffer sharing mode has been turned off, now de-reference pointer  %s!!", __func__);

      r->sourceExitSharingMode();

      isCameraTurnOffBufferSharingMode = true;

      return true;
      }

   return false;
}

bool CameraHardware::requestEnableSharingMode()
{
    isRecordingStarted = false;
    isCameraTurnOffBufferSharingMode = false;
    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());
    return (r->sourceRequestToEnableSharingMode() == BS_SUCCESS?true:false);
}

bool CameraHardware::requestDisableSharingMode()
{
    isRecordingStarted = false;
    isCameraTurnOffBufferSharingMode = false;
    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());
    return (r->sourceRequestToDisableSharingMode() == BS_SUCCESS? true:false);
}
#endif
status_t CameraHardware::startRecording()
{
    LOGD("%s :", __func__);
    Mutex::Autolock lock(mRecordLock);

    for (int i=0; i < kBufferCount; i++) {
        clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
    }

    mRecordRunning = true;
#if ENABLE_BUFFER_SHARE_MODE
    requestEnableSharingMode();
#endif
    return NO_ERROR;
}

void CameraHardware::stopRecording()
{
    LOGD("%s :", __func__);
    Mutex::Autolock lock(mRecordLock);
    mRecordRunning = false;
#if ENABLE_BUFFER_SHARE_MODE
    requestDisableSharingMode();
#endif
}

bool CameraHardware::recordingEnabled()
{
    return mRecordRunning;
}

void CameraHardware::releaseRecordingFrame(const sp<IMemory>& mem)
{
    ssize_t offset = mem->offset();
    size_t size = mem->size();
    int releasedFrame = offset / size;

    clrBF(&mRecordingBuffer.flags[releasedFrame], BF_LOCKED);

    LOG2("a recording frame transfered to client has been released, index %d",
         releasedFrame);
}

// ---------------------------------------------------------------------------

status_t CameraHardware::autoFocus()
{
    LOG1("%s :", __func__);
    //signal autoFocusThread to run once
    mFocusCondition.signal();
    return NO_ERROR;
}

status_t CameraHardware::cancelAutoFocus()
{
    LOGV("%s :", __func__);
    //Add the focus cancel function mCamera->cancelAutofocus here
    Mutex::Autolock lock(mLock);
    mPreviewAeAfAwbRunning = true;
    mPreviewAeAfAwbCondition.signal();
    return NO_ERROR;
}

status_t CameraHardware::touchToFocus(int blockNumber)
{
    LOGD("!!!line:%d, enter touchToFocus", __LINE__);
    return NO_ERROR;
}

status_t CameraHardware::cancelTouchToFocus()
{
    LOGD("!!!line:%d, enter cancelTouchToFocus", __LINE__);
    return cancelAutoFocus();
}

#define MAX_FRAME_WAIT 3
#define FLASH_FRAME_WAIT 4
int CameraHardware::pictureThread()
{
    LOGD("%s :start", __func__);
    int cap_width, cap_height, cap_frame_size;
    int mPostViewWidth, mPostViewHeight, mPostViewSize;


    mCamera->getPostViewSize(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    mCamera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);

    //YUV420 -> RGB
    mPostViewSize = (mPostViewSize * 4 + 2) / 3;
    //For postview
    sp<MemoryBase> pv_buffer = new MemoryBase(mRawHeap, 0, mPostViewSize);

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        int index;
        void *main_out, *postview_out;
        postview_out = mRawHeap->getBase();
        unsigned int page_size = getpagesize();
        unsigned int cap_frame_size_aligned = (cap_frame_size + page_size - 1)
                                              & ~(page_size - 1);

        sp<MemoryHeapBase> picHeap = new MemoryHeapBase(cap_frame_size_aligned);

        if (memory_userptr) {
            mCamera->setSnapshotUserptr(picHeap->getBase(), mRawHeap->getBase());
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&pic_thread_start,  0);
#endif
        //Prepare for the snapshot
        if (mCamera->startSnapshot() < 0)
            goto start_error_out;

#ifdef PERFORMANCE_TUNING
        gettimeofday(&snapshot_start,  0);
#endif
		flushParameters(mParameters);

        //Skip the first frame
        //Don't need the flash for the skip frame
        bool flash_status;
        mCamera->getFlashStatus(&flash_status);
        mCamera->setFlashStatus(false); //turn off flash
        index = mCamera->getSnapshot(&main_out, postview_out);
        mCamera->setFlashStatus(flash_status);
        if (index < 0) {
            picHeap.clear();
            goto get_img_error;
        }
        //Qbuf only if there is no flash. If with the flash, we qbuf after the
        //flash
        if (!flash_status)
            mCamera->putSnapshot(index);

#ifdef PERFORMANCE_TUNING
        gettimeofday(&first_frame, 0);
#endif

        // Modified the shutter sound timing for Jpeg capture
        if (mMsgEnabled & CAMERA_MSG_SHUTTER)
            mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

        //Get the buffer and copy the buffer out
        index = mCamera->getSnapshot(&main_out, postview_out);
        if (index < 0)
            goto get_img_error;
        LOGD("RAW image got: size %dB", cap_frame_size);
        memcpy(picHeap->getBase(), main_out, cap_frame_size);
#ifdef PERFORMANCE_TUNING
        gettimeofday(&second_frame, 0);
#endif

        //Postview
        LOG1("call camera service to postview: size %dK",mPostViewSize/1024);
        mDataCb(CAMERA_MSG_RAW_IMAGE, pv_buffer, mCallbackCookie);
        pv_buffer.clear();

#ifdef PERFORMANCE_TUNING
        gettimeofday(&postview, 0);
#endif
        //Stop the Camera Now
        mCamera->putSnapshot(index);
        mCamera->stopSnapshot();

#ifdef PERFORMANCE_TUNING
        gettimeofday(&snapshot_stop, 0);
#endif
        //software encoding by skia and then copy out to the raw memory
        if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
            int jpegSize;
            if (encodeToJpeg(cap_width, cap_height, picHeap->getBase(),
                             &jpegSize) < 0)
                goto start_error_out;

            sp<MemoryBase> JpegBuffer = new MemoryBase(picHeap, 0, jpegSize);
            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, JpegBuffer, mCallbackCookie);
            JpegBuffer.clear();
        }
#ifdef PERFORMANCE_TUNING
        gettimeofday(&jpeg_encoded, 0);
#endif

        //clean up
        picHeap.clear();
    }
out:
    pv_buffer.clear();
    mStateLock.lock();
    mCaptureInProgress = false;
    mStateLock.unlock();
    LOG1("%s :end", __func__);

    return NO_ERROR;

get_img_error:
    LOGE("Get the snapshot error, now stoping the camera\n");
    mCamera->stopSnapshot();
start_error_out:
    mStateLock.lock();
    mCaptureInProgress = false;
    mStateLock.unlock();
    LOGE("%s :end", __func__);

    return UNKNOWN_ERROR;
}

status_t CameraHardware::encodeToJpeg(int width, int height, void *buf, int *jsize)
{
    SkDynamicMemoryWStream *stream = NULL;
    SkBitmap *bitmap = NULL;
    SkImageEncoder* encoder = NULL;

    stream = new SkDynamicMemoryWStream;
    if (stream == NULL) {
        LOGE("%s: No memory for stream\n", __func__);
        goto stream_error;
    }

    bitmap = new SkBitmap();;
    if (bitmap == NULL) {
        LOGE("%s: No memory for bitmap\n", __func__);
        goto bitmap_error;
    }

    encoder = SkImageEncoder::Create(SkImageEncoder::kJPEG_Type);
    if (encoder != NULL) {
        bool success = false;
        bitmap->setConfig(SkBitmap::kRGB_565_Config, width, height);
        bitmap->setPixels(buf, NULL);
        success = encoder->encodeStream(stream, *bitmap, 75);
        *jsize = stream->getOffset();
        stream->copyTo(buf);
        LOG1("%s: jpeg encode result:%d, size:%d", __func__, success, *jsize);
        delete stream;
        delete bitmap;
        delete encoder;
    } else {
        LOGE("%s: No memory for encoder\n", __func__);
        goto encoder_error;
    }
    //Send the data out
    return 0;
encoder_error:
    delete bitmap;
bitmap_error:
    delete stream;
stream_error:
    return -1;
}


status_t CameraHardware::takePicture()
{
    LOG1("%s\n", __func__);

#ifdef PERFORMANCE_TUNING
    gettimeofday(&picture_start, 0);
#endif
    disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    // Set the Flash
    mCamera->setFlash();
    stopPreview();
    mCamera->clearFlash();
#ifdef PERFORMANCE_TUNING
    gettimeofday(&preview_stop, 0);
#endif
    enableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    setSkipFrame(CAPTURE_SKIP_FRAME);
#ifdef PERFORMANCE_TUNING
    gettimeofday(&preview_stop, 0);
#endif
    if (mCaptureInProgress) {
        LOGE("%s : capture already in progress", __func__);
        return INVALID_OPERATION;
    }

    if (mPictureThread->run("CameraPictureThread", PRIORITY_DEFAULT) !=
            NO_ERROR) {
        LOGE("%s : couldn't run picture thread", __func__);
        return INVALID_OPERATION;
    }
    mCaptureInProgress = true;

    return NO_ERROR;
}

status_t CameraHardware::cancelPicture()
{
    LOG1("%s start\n", __func__);
    mPictureThread->requestExitAndWait();
    return NO_ERROR;
}

int CameraHardware::autoFocusThread()
{
    int count = 0;
    int af_status = 0;
    LOG1("%s : starting", __func__);
    mFocusLock.lock();
    //check early exit request
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        LOGV("%s : exiting on request0", __func__);
        return NO_ERROR;
    }
    mFocusCondition.wait(mFocusLock);
    /* check early exit request */
    if (mExitAutoFocusThread) {
        mFocusLock.unlock();
        LOGV("%s : exiting on request1", __func__);
        return NO_ERROR;
    }
    mFocusLock.unlock();

    //stop the preview 3A thread
    mAeAfAwbLock.lock();
    mPreviewAeAfAwbRunning = false;
    mAeAfAwbLock.unlock();
    LOGV("%s: begin do the autofocus\n", __func__);
    //Do something here to call the mCamera->autofocus
    //
    mCamera->setStillAfStatus(true);
    af_status = mCamera->runStillAfSequence();
    mCamera->setStillAfStatus(false);
    mFocusLock.lock();
    mFocusLock.unlock();
    //FIXME Always success?
    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, af_status , 0, mCallbackCookie);
    LOG1("%s : exiting with no error", __func__);
    return NO_ERROR;
}

status_t CameraHardware::sendCommand(int32_t command, int32_t arg1,
                                     int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardware::release()
{
    LOGD("%s start:", __func__);
    if (mPreviewThread != NULL) {
        mPreviewThread->requestExit();
        mExitPreviewThread = true;
        mPreviewRunning = true; /* let it run so it can exit */
        mPreviewCondition.signal();
        mPreviewThread->requestExitAndWait();
        mPreviewThread.clear();
    }
    LOG1("%s deleted the preview thread:", __func__);

    if (mAutoFocusThread != NULL) {
        mFocusLock.lock();
        mAutoFocusThread->requestExit();
        mExitAutoFocusThread = true;
        mFocusLock.unlock();
        mFocusCondition.signal();
        mAutoFocusThread->requestExitAndWait();
        mAutoFocusThread.clear();
    }
    LOG1("%s deleted the autofocus thread:", __func__);

    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
    }
    LOG1("%s deleted the picture thread:", __func__);

    if (mAeAfAwbThread != NULL) {
        mAeAfAwbThread->requestExit();
        mPreviewAeAfAwbRunning = true;
        mPreviewAeAfAwbCondition.signal();
        mPreviewFrameLock.lock();
        mExitAeAfAwbThread = true;
        mPreviewFrameLock.unlock();
        mPreviewFrameCondition.signal();
        LOG1("%s waiting 3A thread to exit:", __func__);
        mAeAfAwbThread->requestExitAndWait();
        mAeAfAwbThread.clear();
    }
    LOG1("%s deleted the 3A thread:", __func__);
}

status_t CameraHardware::dump(int fd, const Vector<String16>& args) const
{
    LOG2("%s",__func__);
    return NO_ERROR;
}

/* This function is needed when parameters saved by app need to be flushed to
 * the driver */
int CameraHardware::flushParameters(const CameraParameters& params)
{
	const char *value;
	int ret = 0;

	int effect;
	value = params.get("effect");
	LOGD(" - set effect:%s\n", value);
	if(!strcmp(value, CameraParameters::EFFECT_MONO))
		effect = V4L2_COLORFX_BW;
	else if(!strcmp(value, CameraParameters::EFFECT_NEGATIVE))
		effect = V4L2_COLORFX_NEGATIVE;
	else if(!strcmp(value, CameraParameters::EFFECT_SEPIA))
		effect = V4L2_COLORFX_SEPIA;
	else
		effect = V4L2_COLORFX_NONE;

	mCamera->setColorEffect(effect);

	return ret;
}

status_t CameraHardware::setParameters(const CameraParameters& params)
{
    int ret;
    Mutex::Autolock lock(mLock);
    // XXX verify params

    //params.dump();  // print parameters for debug

    CameraParameters p = params;

    //Check and set the new preview format
    int new_preview_width, new_preview_height;
    int    new_preview_format = 0;
    const char *new_value, *set_value;

    p.getPreviewSize(&new_preview_width, &new_preview_height);
    new_value = p.getPreviewFormat();
    set_value = mParameters.getPreviewFormat();

    if (strcmp(new_value, "yuv420sp") == 0) {
        new_preview_format = V4L2_PIX_FMT_NV12;
    }  else if (strcmp(new_value, "yuv422i-yuyv") == 0) {
        new_preview_format = V4L2_PIX_FMT_YUYV;
    } else if (strcmp(new_value, "rgb565") == 0) {
        new_preview_format = V4L2_PIX_FMT_RGB565;
    } else {
        LOGD("only yuv420sp, yuv422i-yuyv, rgb565 preview are supported, use rgb565");
        new_preview_format = V4L2_PIX_FMT_RGB565;
    }

    if (0 < new_preview_width && 0 < new_preview_height && new_value != NULL) {
        LOGD(" - Preview pixel format = new \"%s\"  / current \"%s\"",
             new_value, set_value);

        if (mCamera->setPreviewSize(new_preview_width, new_preview_height,
                                    new_preview_format) < 0) {
            LOGE("ERR(%s):Fail on setPreviewSize(width(%d), height(%d), format(%d))",
                 __func__, new_preview_width, new_preview_height, new_preview_format);
        } else {
            p.setPreviewSize(new_preview_width, new_preview_height);
            p.setPreviewFormat(new_value);
            LOGD("     ++ Changed Preview Pixel Format to %s",p.getPreviewFormat());
        }
    }

    // preview frame rate
    int new_fps = p.getPreviewFrameRate();
    int set_fps = mParameters.getPreviewFrameRate();
    LOGD(" - FPS = new \"%d\" / current \"%d\"",new_fps, set_fps);
    if (new_fps != set_fps) {
        p.setPreviewFrameRate(new_fps);
        LOGD("     ++ Changed FPS to %d",p.getPreviewFrameRate());
    }
    LOGD("PREVIEW SIZE: %dx%d, FPS: %d", new_preview_width, new_preview_height,
         new_fps);

    //Picture format
    int new_picture_width, new_picture_height;
    const char *new_format = p.getPictureFormat();
    if (strcmp(new_format, "jpeg") == 0)
        mPicturePixelFormat = V4L2_PIX_FMT_RGB565;
    else {
        LOGE("Only jpeg still pictures are supported");
    }

    LOGD(" - Picture pixel format = new \"%s\"", new_format);
    p.getPictureSize(&new_picture_width, &new_picture_height);
    LOGD("%s : new_picture_width %d new_picture_height = %d", __func__,
         new_picture_width, new_picture_height);
    if (0 < new_picture_width && 0 < new_picture_height) {
        if (mCamera->setSnapshotSize(new_picture_width, new_picture_height,
                                     mPicturePixelFormat) < 0) {
            LOGE("ERR(%s):Fail on mCamera->setSnapshotSize(width(%d), height(%d))",
                 __func__, new_picture_width, new_picture_height);
            ret = UNKNOWN_ERROR;
        } else {
            p.setPictureSize(new_picture_width, new_picture_height);
            p.setPictureFormat(new_value);
        }
    }
    //Video recording
    //FIXME: protect mVideoPreviewEnabled for contension.
    int vfmode = p.getInt("camera-mode");
    int mVideoFormat;
    //Deternmine the current viewfinder MODE.
    if (vfmode == 1) {
        LOG1("%s: Entering the video recorder mode\n", __func__);
        Mutex::Autolock lock(mRecordLock);
        mVideoPreviewEnabled = true; //viewfinder running in video mode
        mVideoFormat = V4L2_PIX_FMT_NV12;
    } else {
        LOG1("%s: Entering the normal preview mode\n", __func__);
        Mutex::Autolock lock(mRecordLock);
        mVideoFormat = V4L2_PIX_FMT_NV12;
        mVideoPreviewEnabled = false; //viewfinder running in preview mode
    }

    //Use picture size as the recording size . Video format should can be
    //configured FIXME
    int pre_width, pre_height, pre_size, rec_w, rec_h;
    mCamera->getPreviewSize(&pre_width, &pre_height, &pre_size);
    p.getRecordingSize(&rec_w, &rec_h);
    if(checkRecording(rec_w, rec_h)) {
        LOGD("line:%d, before setRecorderSize. w:%d, h:%d, format:%d", __LINE__, rec_w, rec_h, mVideoFormat);
        mCamera->setRecorderSize(rec_w, rec_h, mVideoFormat);
    }
    else {
        LOGD("line:%d, before setRecorderSize. w:%d, h:%d, format:%d", __LINE__, pre_width, pre_height, mVideoFormat);
        mCamera->setRecorderSize(pre_width, pre_height, mVideoFormat);
    }

    //Jpeg
    if ( (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) || (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) ) {
        int jpeg_quality = p.getInt("jpeg-quality");
        new_value = p.get("jpeg-quality");
        set_value = mParameters.get("jpeg-quality");
        LOG1(" - jpeg-quality = new \"%s\" (%d) / current \"%s\"",new_value, jpeg_quality, set_value);
        if (strcmp(set_value, new_value) != 0) {
            p.set("jpeg-quality", new_value);
            LOG1("     ++ Changed jpeg-quality to %s",p.get("jpeg-quality"));
            //mCamera->setJPEGRatio(p.get("jpeg-quality"));
        }
    }

    // Color Effect
	int effect = p.getInt("effect");
	new_value = p.get("effect");
	set_value = mParameters.get("effect");
	LOGD(" - effect = new \"%s\" (%d) / current \"%s\"",new_value, effect, set_value);
	if (strcmp(set_value, new_value) != 0) {
		p.set("effect", new_value);
		LOGD("     ++ Changed effect to %s",p.get("effect"));
		if(!strcmp(new_value, CameraParameters::EFFECT_MONO))
			effect = V4L2_COLORFX_BW;
		else if(!strcmp(new_value, CameraParameters::EFFECT_NEGATIVE))
			effect = V4L2_COLORFX_NEGATIVE;
		else if(!strcmp(new_value, CameraParameters::EFFECT_SEPIA))
			effect = V4L2_COLORFX_SEPIA;
		else
			effect = V4L2_COLORFX_NONE;
			mCamera->setColorEffect(effect);
	}

    // white balance
    const char * pwb = CameraParameters::KEY_WHITE_BALANCE;
    int whitebalance = p.getInt(pwb);
    new_value = p.get(pwb);
    set_value = mParameters.get(pwb);
    LOG1(" - whitebalance = new \"%s\" (%d) / current \"%s\"", new_value,
         whitebalance, set_value);
    if (strcmp(set_value, new_value) != 0) {
        int wb_mode;
        p.set(pwb, new_value);

        if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_AUTO))
            wb_mode = CAM_AWB_MODE_AUTO;
        else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_INCANDESCENT))
            wb_mode = CAM_AWB_MODE_WARM_INCANDESCENT;
        else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_FLUORESCENT))
            wb_mode = CAM_AWB_MODE_FLUORESCENT;
        else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_WARM_FLUORESCENT))
            wb_mode = CAM_AWB_MODE_WARM_FLUORESCENT;
        else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_DAYLIGHT))
            wb_mode = CAM_AWB_MODE_DAYLIGHT;
        else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_CLOUDY_DAYLIGHT))
            wb_mode = CAM_AWB_MODE_CLOUDY;
        else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_TWILIGHT))
            wb_mode = CAM_AWB_MODE_SUNSET;
        else if(!strcmp(new_value, CameraParameters::WHITE_BALANCE_SHADE))
            wb_mode = CAM_AWB_MODE_SHADOW;
        else
            wb_mode = CAM_AWB_MODE_AUTO;
        mAAA->AwbSetMode(wb_mode);

        LOG1("     ++ Changed whitebalance to %s, wb_mode:%d\n",p.get(pwb), wb_mode);
    }

    //EV_compensation
    const char * pexp = CameraParameters::KEY_EXPOSURE_COMPENSATION;
    int exposure = p.getInt(pexp);
    new_value = p.get(pexp);
    set_value = mParameters.get(pexp);
    LOG1(" -  = new \"%s\" (%d) / current \"%s\"",new_value, effect, set_value);
    if (strcmp(set_value, new_value) != 0) {
        p.set(pexp, new_value);
        mAAA->AeSetEv(atoi(new_value));
        int ev = 0;
        mAAA->AeGetEv(&ev);
        LOG1("      ++Changed exposure effect to %s",p.get(pexp));
    }

    //process zoom
    int zoom = p.getInt("zoom");
    mCamera->set_zoom_val(zoom);

    //Focus Mode
    const char * pfocusmode = CameraParameters::KEY_FOCUS_MODE;
    int focus_mode = p.getInt(pfocusmode);
    new_value = p.get(pfocusmode);
    set_value = mParameters.get(pfocusmode);
    LOG1(" - focus-mode = new \"%s\" (%d) / current \"%s\"", new_value, focus_mode, set_value);
    if (strcmp(set_value, new_value) != 0) {
        int afmode;
        p.set(pfocusmode, new_value);

        if(!strcmp(new_value, CameraParameters::FOCUS_MODE_AUTO))
            afmode = CAM_FOCUS_MODE_AUTO;
        else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_INFINITY))
            afmode = CAM_FOCUS_MODE_NORM;
        else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_MACRO))
            afmode = CAM_FOCUS_MODE_MACRO;
        else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO))
            afmode = CAM_FOCUS_MODE_AUTO;
        else
            afmode = CAM_FOCUS_MODE_AUTO;
        mAAA->AfSetMode(afmode);

        LOG1("     ++ Changed focus-mode to %s, afmode:%d",p.get(pfocusmode), afmode);
    }

    //Flicker Mode
    const char * pantibanding = CameraParameters::KEY_ANTIBANDING;
    int antibanding = p.getInt(pantibanding);
    new_value = p.get(pantibanding);
    set_value = mParameters.get(pantibanding);
    LOG1(" - antibanding = new \"%s\" (%d) / current \"%s\"", new_value, antibanding, set_value);
    if (strcmp(set_value, new_value) != 0) {
        int bandingval;
        p.set(pantibanding, new_value);

        if(!strcmp(new_value, CameraParameters::ANTIBANDING_AUTO))
            bandingval = CAM_AEFLICKER_MODE_AUTO;
        else if(!strcmp(new_value, CameraParameters::ANTIBANDING_50HZ))
            bandingval = CAM_AEFLICKER_MODE_50HZ;
        else if(!strcmp(new_value, CameraParameters::ANTIBANDING_60HZ))
            bandingval = CAM_AEFLICKER_MODE_60HZ;
        else if(!strcmp(new_value, CameraParameters::ANTIBANDING_OFF))
            bandingval = CAM_AEFLICKER_MODE_OFF;
        else
            bandingval = CAM_AEFLICKER_MODE_AUTO;
        mAAA->AfSetMode(bandingval);

        LOG1("     ++ Changed focus-mode to %s, antibanding val:%d",p.get(pantibanding), bandingval);
    }

    int rotation = p.getInt("rotation");
    new_value = p.get("rotation");
    set_value = mParameters.get("rotation");
    LOG1(" - rotation = new \"%s\" (%d) / current \"%s\"", new_value, rotation, set_value);
    if (strcmp(set_value, new_value) != 0) {
        p.set("rotation", new_value);
        LOG1("     ++ Changed rotation to %s",p.get("rotation"));
    }

    // Scene Mode
    int scene_mode = p.getInt("scene-mode");
    new_value = p.get("scene-mode");
    set_value = mParameters.get("scene-mode");
    LOG1(" - scene-mode = new \"%s\" (%d) / current \"%s\"", new_value,
         scene_mode, set_value);
    if (strcmp(set_value, new_value) != 0) {
        p.set("scene-mode", new_value);
        LOG1("     ++ Changed scene-mode to %s",p.get("scene-mode"));
        if (!strcmp (new_value, "auto")) {
            scene_mode = CAM_SCENE_MODE_AUTO;
        }
        else if (!strcmp (new_value, "portrait")) {
            scene_mode = CAM_SCENE_MODE_PORTRAIT;
        }
        else if (!strcmp (new_value, "sports")) {
            scene_mode = CAM_SCENE_MODE_SPORTS;
        }
        else if (!strcmp (new_value, "landscape")) {
            scene_mode = CAM_SCENE_MODE_LANDSCAPE;
        }
        else if (!strcmp (new_value, "night")) {
            scene_mode = CAM_SCENE_MODE_NIGHT;
        }
        else if (!strcmp (new_value, "fireworks")) {
            scene_mode = CAM_SCENE_MODE_FIREWORKS;
        }
        else {
            scene_mode = CAM_SCENE_MODE_AUTO;
            LOG1("     ++ Not supported scene-mode");
        }
        mAAA->AeSetSceneMode (scene_mode);
    }

    //flash mode
    int flash_mode = p.getInt("flash-mode");
    new_value = p.get("flash-mode");
    set_value = mParameters.get("flash-mode");
    LOG1(" - flash-mode = new \"%s\" (%d) / current \"%s\"", new_value, flash_mode, set_value);
    if (strcmp(set_value, new_value) != 0) {
        p.set("flash-mode", new_value);
        LOG1("     ++ Changed flash-mode to %s",p.get("flash-mode"));

        if (!strcmp (new_value, "auto")) {
            flash_mode = CAM_FLASH_MODE_AUTO;
        }
        else if (!strcmp (new_value, "off")) {
            flash_mode = CAM_FLASH_MODE_OFF;
        }
        else if (!strcmp (new_value, "on")) {
            flash_mode = CAM_FLASH_MODE_ON;
        }
        else {
            flash_mode = CAM_FLASH_MODE_AUTO;
            LOG1("     ++ Not supported flash-mode");
        }
        mAAA->AeSetFlashMode (flash_mode);
    }

    //touch Focus (focus windows)
    int x_left, x_right, y_top, y_bottom;
    x_left = p.getInt("touchfocus-x-left");
    x_right = p.getInt("touchfocus-x-right");
    y_top = p.getInt("touchfocus-x-top");
    y_bottom = p.getInt("touchfocus-x-bottom");

    //Update the parameters
    mParameters = p;
    return NO_ERROR;
}

CameraParameters CameraHardware::getParameters() const
{
    Mutex::Autolock lock(mLock);
    return mParameters;
}

wp<CameraHardwareInterface> CameraHardware::singleton;

sp<CameraHardwareInterface> CameraHardware::createInstance(int cameraId)
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardware(cameraId));
    singleton = hardware;
    return hardware;
}

//----------------------------------------------------------------------------
//----------------------------HAL--used for camera service--------------------
static int HAL_cameraType[MAX_CAMERAS];
static CameraInfo HAL_cameraInfo[MAX_CAMERAS] = {
    {
        CAMERA_FACING_BACK,
        90,  /* orientation */
    },
    {
        CAMERA_FACING_FRONT,
        90,  /* orientation */
    }
};

extern "C" int HAL_checkCameraType(unsigned char *name) {
    return SENSOR_TYPE_RAW;
}

/* This function will be called when the camera service is created.
 * Do some init work in this function.
 */
extern "C" int HAL_getNumberOfCameras()
{
    return sizeof(HAL_cameraInfo) / sizeof(HAL_cameraInfo[0]);
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
{
    memcpy(cameraInfo, &HAL_cameraInfo[cameraId], sizeof(CameraInfo));
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
    return CameraHardware::createInstance(cameraId);
}

}; // namespace android
