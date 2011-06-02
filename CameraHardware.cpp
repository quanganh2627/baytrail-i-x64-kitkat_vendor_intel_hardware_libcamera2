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

#include <string.h>

#if ENABLE_BUFFER_SHARE_MODE
#include <IntelBufferSharing.h>
#endif

#define MAX_CAMERAS 2		// Follow CamreaService.h
#define SENSOR_TYPE_SOC 0
#define SENSOR_TYPE_RAW 1

#define ASSIST_INTENSITY_WORKING       (3*2000)
#define ASSIST_INTENSITY_OFF 0

#define INDICATOR_INTENSITY_WORKING       (3*2500)
#define INDICATOR_INTENSITY_OFF 0

namespace android {

#define CAMERA_MSG_TOUCH_TO_FOCUS 0x200

static const int INITIAL_SKIP_FRAME = 4;
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
    isVideoStarted = false;
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
    p.set("preview-size-values", "640x480,640x360");
    p.set("picture-format-values", "jpeg");

    char pchar[10];
    sprintf(pchar, "%d", mJpegQualityDefault);
    p.set(CameraParameters::KEY_JPEG_QUALITY, pchar);
    sprintf(pchar, "%d", mJpegThumbnailQualityDefault);
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, pchar);

    p.set(CameraParameters::KEY_AE_MODE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_AE_MODES, "auto,manual,shutter-priority,aperture-priority");
    // focus mode
    p.set(CameraParameters::KEY_FOCUS_MODE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto,infinity,macro,touch,manual");
    // balance mode
    p.set(CameraParameters::KEY_WHITE_BALANCE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, "auto,incandescent,fluorescent,daylight,cloudy-daylight,manual");
    // scene mode
    p.set(CameraParameters::KEY_SCENE_MODE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, "auto,portrait,sports,landscape,night,fireworks");
    // exposure compensation
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION, "0");
    p.set(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION, "6");
    p.set(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION, "-6");
    p.set(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP, "0.33333333");
    // flash mode
    p.set(CameraParameters::KEY_FLASH_MODE,"off");
    p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"auto,off,on,torch,slow-sync,day-sync");
    // flicker mode
    p.set(CameraParameters::KEY_ANTIBANDING, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, "off,50hz,60hz,auto");
    // ae metering mode
    p.set(CameraParameters::KEY_AE_METERING_MODE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_AE_METERING_MODES, "auto,spot,center,customized");
    // af metering mode
    p.set(CameraParameters::KEY_AF_METERING_MODE,"auto");
    p.set(CameraParameters::KEY_SUPPORTED_AF_METERING_MODES,"auto,spot");
    // ae lock mode
    p.set(CameraParameters::KEY_AE_LOCK_MODE,"unlock");
    p.set(CameraParameters::KEY_SUPPORTED_AE_LOCK_MODES,"lock,unlock");
    // back lighting correction
    p.set(CameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE,"off");
    p.set(CameraParameters::KEY_SUPPORTED_BACK_LIGHTING_CORRECTION_MODES,"on,off");
    // red eye removal
    p.set(CameraParameters::KEY_RED_EYE_MODE,"off");
    p.set(CameraParameters::KEY_SUPPORTED_RED_EYE_MODES,"on,off");
    // awb mapping
    p.set(CameraParameters::KEY_AWB_MAPPING_MODE, "indoor");
    p.set(CameraParameters::KEY_SUPPORTED_AWB_MAPPING_MODES, "indoor,outdoor");
    // manual shutter control
    p.set(CameraParameters::KEY_SHUTTER, "60");
    p.set(CameraParameters::KEY_SUPPORTED_SHUTTER, "2s,1s,2,4,8,15,30,60,125,250,500");
    // manual aperture control
    p.set(CameraParameters::KEY_APERTURE, "f2.8");
    p.set(CameraParameters::KEY_SUPPORTED_APERTURE, "f2.8,f4,f5.6,f8,f11,f16");
    // manual iso control
    p.set(CameraParameters::KEY_ISO, "iso-200");
    p.set(CameraParameters::KEY_SUPPORTED_ISO, "iso-100,iso-200,iso-400,iso-800");
    // manual color temperature
    p.set(CameraParameters::KEY_COLOR_TEMPERATURE, "5000");
    // manual focus
    p.set(CameraParameters::KEY_FOCUS_DISTANCES, "2,2,2");
    // focus window
    p.set("focus-window", "0,0,0,0");

    p.set("video-frame-format","yuv420sp");
    p.set("zoom-supported", "true");
    p.set("max-zoom", "64");
    p.set("zoom-ratios", "0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,"
          "22,23,24,24,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,"
          "45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64");
    p.set("zoom", 0);

    p.set("effect", "none");
    p.set("effect-values","none,mono,negative,sepia");
    p.set("xnr", "false");
    p.set("xnr-values", "true,false");
    p.set("digital-image-stablization", "off");
    p.set("digital-image-stablization-values", "on,off");
    p.set("temporal-noise-reduction", "off");
    p.set("temporal-noise-reduction-values", "on,off");
    p.set("noise-reduction-and-edge-enhancement", "on");
    p.set("noise-reduction-and-edge-enhancement-values", "on,off");
    p.set("multi-access-color-correction", "enhance-none");
    p.set("multi-access-color-correction-values", "enhance-sky,enhance-grass,enhance-skin,enhance-nono");

    if (mCameraId == CAMERA_FACING_BACK) {
        // For main back camera
        p.set("picture-size-values","320x240,640x480,1024x768,1280x720,"
              "1920x1080,2048x1536,2560x1920,3264x2448,3648x2736,4096x3072,4352x3264");
    } else {
        // For front camera
        p.set("picture-size-values","320x240,640x480,1280x720,1920x1080");
    }

    mParameters = p;

    mFlush3A = true;
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
        mRawIdHeap = new MemoryHeapBase(sizeof(int));
        mRawIdBase = new MemoryBase(mRawIdHeap, 0, sizeof(int));
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
    mRawIdBase.clear();
    mRawIdHeap.clear();
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
#define W_480P 768
#define H_480P 480
#define W_720P  1280
#define H_720P  720
#define W_1080P 1920
#define H_1080P 1080
        if ((W_480P == width) && (H_480P == height))
            return true;
        if ((W_720P == width) && (H_720P == height))
            return true;
        if ((W_1080P == width) && (H_1080P == height))
            return true;

        return false;
}
void CameraHardware::initRecordingBuffer(int size, int padded_size)
{
    //Init the preview stream buffer first
    int w, h, preview_size, preview_padded_size;
    unsigned int page_size = getpagesize();
    unsigned int size_aligned = (padded_size + page_size - 1) & ~(page_size - 1);
    unsigned int ptr_size = sizeof(unsigned char*);
    mPreviewFrame = 0;
    mPostPreviewFrame = 0;
    mCamera->getPreviewSize(&w, &h, &preview_size, &preview_padded_size);
    initPreviewBuffer(preview_padded_size);

    //Init the video stream buffer
    if (mRecordingBuffer.heap != NULL)
        deInitRecordingBuffer();

    mRecordingBuffer.heap = new MemoryHeapBase(size_aligned * kBufferCount);
    mUserptrHeap = new MemoryHeapBase(ptr_size * kBufferCount);
    for (int i = 0; i < kBufferCount; i++) {
        mRecordingBuffer.flags[i] = 0;
        mRecordingBuffer.base[i] = new MemoryBase(mRecordingBuffer.heap,
                i * size_aligned, size);
        mRecordingBuffer.start[i] = (uint8_t *)mRecordingBuffer.heap->base()
                                    + (i * size_aligned);
        mUserptrBase[i] = new MemoryBase(mUserptrHeap, i * ptr_size, ptr_size);
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
        for (int i = 0; i < kBufferCount; i++) {
            mRecordingBuffer.base[i].clear();
            mUserptrBase[i].clear();
        }
        mRecordingBuffer.heap->dispose();
        mRecordingBuffer.heap.clear();
        mUserptrHeap.clear();
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
    }
    mPreviewFrame = (previewFrame + 1) % kBufferCount;
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

void CameraHardware::processRecordingFrame(void *buffer, int index)
{
    if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
        //Copy buffer out from driver
        int recordingFrame = index;

        if (!isBFSet(mRecordingBuffer.flags[recordingFrame], BF_ENABLED) &&
            !isBFSet(mRecordingBuffer.flags[recordingFrame], BF_LOCKED)) {
            setBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
#if ENABLE_BUFFER_SHARE_MODE
#else
            memcpy(mRecordingBuffer.start[recordingFrame], buffer,
                   mRecorderFrameSize);
#endif
            clrBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
            setBF(&mRecordingBuffer.flags[recordingFrame],BF_ENABLED);
        }

        //Notify the client of a new recording frame.
        int postRecordingFrame = index;
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

#if ENABLE_BUFFER_SHARE_MODE
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME,
                             mUserptrBase[postRecordingFrame], mCallbackCookie);
#else
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME,
                         mRecordingBuffer.base[postRecordingFrame], mCallbackCookie);
#endif
            LOG2("Sending the recording frame, size %d, index %d/%d\n",
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

    //Skip the first several frames from the sensor
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
    bool bufferIsReady = false;
    //Check the buffer sharing

#if ENABLE_BUFFER_SHARE_MODE
    if (mRecordRunning) {
        if(NO_ERROR == getSharedBuffer() && !checkSharedBufferModeOff())
            bufferIsReady = true;
    }
#endif

    mDeviceLock.lock();
    int index = mCamera->getRecording(&main_out, &preview_out);
    mDeviceLock.unlock();
    if (index < 0) {
        LOGE("ERR(%s):Fail on mCamera->getRecording()", __func__);
        return -1;
    }
    //Run 3A after each frame
    mPreviewFrameCondition.signal();

    //Skip the first several frames from the sensor
    mSkipFrameLock.lock();
    if (mSkipFrame > 0) {
        mSkipFrame--;
        mSkipFrameLock.unlock();
        mCamera->putRecording(index);
        return NO_ERROR;
    }
    mSkipFrameLock.unlock();

    //Process the preview frame first
    processPreviewFrame(preview_out);

    //Process the recording frame when recording started
    if (mRecordRunning && bufferIsReady)
        processRecordingFrame(main_out, index);
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
        int w, h, size, padded_size;
        LOGD("Start recording preview\n");
        mRecordingFrame = 0;
        mPostRecordingFrame = 0;
        mCamera->getRecorderSize(&w, &h, &size, &padded_size);
        initRecordingBuffer(size, padded_size);
        ret = mCamera->startCameraRecording();
    } else {
        LOGD("Start normal preview\n");
        int w, h, size, padded_size;
        mPreviewFrame = 0;
        mPostPreviewFrame = 0;
        mCamera->getPreviewSize(&w, &h, &size, &padded_size);
        initPreviewBuffer(padded_size);
        ret = mCamera->startCameraPreview();
    }
    if (ret < 0) {
        mPreviewRunning = false;
        mPreviewLock.unlock();
        mPreviewCondition.signal();
        LOGE("ERR(%s):Fail on mCamera->startPreview()", __func__);
        return -1;
    }
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

    LOGD("Stopped the 3A now\n");
    //Tell preview to stop
    mPreviewRunning = false;

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
   if ((!isVideoStarted) && mRecordRunning) {
       int bufferCount;
       unsigned char *pointer;
       SharedBufferType *pSharedBufferInfoArray = NULL;
       android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());

       LOGD("camera try to get share buffer array information");
       r->sourceEnterSharingMode();
       r->sourceGetSharedBuffer(NULL, &bufferCount);

       pSharedBufferInfoArray = (SharedBufferType *)malloc(sizeof(SharedBufferType) * bufferCount);
       if(!pSharedBufferInfoArray) {
           LOGE(" pShareBufferInfoArray malloc failed! ");
           return -1;
       }

       r->sourceGetSharedBuffer(pSharedBufferInfoArray, &bufferCount);
       LOGD ("camera have already gotten share buffer array information");

       if(bufferCount > kBufferCount) {
           bufferCount = kBufferCount;
       }

       unsigned int ptr_size = sizeof(unsigned char*);

       for(int i = 0; i < bufferCount; i ++) {
          mRecordingBuffer.pointerArray[i] = pSharedBufferInfoArray[i].pointer;
          LOGD ("pointer[%d] = %p (%dx%d - stride %d) ", i,
               mRecordingBuffer.start[i], pSharedBufferInfoArray[i].width,
               pSharedBufferInfoArray[i].height,
               pSharedBufferInfoArray[i].stride);
          //Initialize the mUserptrBase again with new userptr
          memcpy((uint8_t *)(mUserptrHeap->base()) + i * ptr_size,
                 &mRecordingBuffer.pointerArray[i], ptr_size);
          memset(mRecordingBuffer.pointerArray[i], 1, mRecorderFrameSize);
       }

       if (mCamera->updateRecorderUserptr(bufferCount,
                            (unsigned char **)mRecordingBuffer.pointerArray) < 0) {
           LOGE ("%s: update recordier userptr failed\n", __func__);
           delete [] pSharedBufferInfoArray;
           return -1;
       }

       delete [] pSharedBufferInfoArray;

       isVideoStarted = true;
    }

    return NO_ERROR;
}

bool CameraHardware::checkSharedBufferModeOff()
{
   /* check whether encoder have send signal to stop buffer sharing mode.*/
   if(isCameraTurnOffBufferSharingMode) {
       LOGD("isCameraTurnOffBufferSharingMode == true");
       return true;
    }

    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());

    if(!isCameraTurnOffBufferSharingMode
        && false == r->isBufferSharingModeSet()) {
        LOGD("buffer sharing mode has been turned off,"
             "now de-reference pointer  %s", __func__);
        mCamera->updateRecorderUserptr(kBufferCount, (unsigned char **)mRecordingBuffer.start);
        r->sourceExitSharingMode();

        isCameraTurnOffBufferSharingMode = true;

        return true;
    }
   return false;
}

bool CameraHardware::requestEnableSharingMode()
{
    isVideoStarted = false;
    isCameraTurnOffBufferSharingMode = false;
    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());
    return (r->sourceRequestToEnableSharingMode() == BS_SUCCESS?true:false);
}

bool CameraHardware::requestDisableSharingMode()
{
    isVideoStarted = false;
    isCameraTurnOffBufferSharingMode = true;
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
    if(CAM_AE_FLASH_MODE_TORCH== mCamera->getFlashMode())
        mCamera->setAssistIntensity(ASSIST_INTENSITY_WORKING);
    else if(CAM_AE_FLASH_MODE_OFF == mCamera->getFlashMode())
        mCamera->setIndicatorIntensity(INDICATOR_INTENSITY_WORKING);
#if ENABLE_BUFFER_SHARE_MODE
    requestEnableSharingMode();
#endif
    return NO_ERROR;
}

void CameraHardware::stopRecording()
{
    LOG1("%s :", __func__);
    Mutex::Autolock lock(mRecordLock);
    mRecordRunning = false;
    if(CAM_AE_FLASH_MODE_TORCH == mCamera->getFlashMode())
        mCamera->setAssistIntensity(ASSIST_INTENSITY_OFF);
    else if(CAM_AE_FLASH_MODE_OFF == mCamera->getFlashMode())
        mCamera->setIndicatorIntensity(INDICATOR_INTENSITY_OFF);

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
    // check if IMemory is NULL
    if (mem == NULL || mem.get() == NULL) {
        LOGE("%s: mem is NULL", __func__);
        return;
    }

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
    LOGD("enter touchToFocus");
    return NO_ERROR;
}

status_t CameraHardware::cancelTouchToFocus()
{
    LOGD("enter cancelTouchToFocus");
    return cancelAutoFocus();
}

void CameraHardware::exifAttributeOrientation(exif_attribute_t& attribute)
{
    // the orientation information will pass from the application. here map it.
    // relative relationship between gsensor orientation and sensor's angle.
    int rotation = mParameters.getInt(CameraParameters::KEY_ROTATION);
    struct CameraInfo cam_info;
    attribute.orientation = 1;
    HAL_getCameraInfo(mCameraId, &cam_info);
    if (CAMERA_FACING_BACK == mCameraId) {  // main sensor
        if (0 == rotation)
            attribute.orientation = 1;
        else if (90 == rotation)
            attribute.orientation = 8;
        else if (180 == rotation)
            attribute.orientation = 3;
        else if (270 == rotation)
            attribute.orientation = 6;
    } else if (CAMERA_FACING_FRONT == mCameraId) { // sub sensor
        if (0 == rotation)
            attribute.orientation = 1;
        else if (90 == rotation)
            attribute.orientation = 6;
        else if (180 == rotation)
            attribute.orientation = 3;
        else if (270 == rotation)
            attribute.orientation = 8;
    }
    LOG1("exifAttribute, sensor angle:%d degrees, rotation value:%d degrees, orientation value:%d",
        cam_info.orientation, rotation, attribute.orientation);
}

void CameraHardware::exifAttributeGPS(exif_attribute_t& attribute)
{
    bool gps_en = true;
    const char *platitude = mParameters.get(CameraParameters::KEY_GPS_LATITUDE);
    const char *plongitude = mParameters.get(CameraParameters::KEY_GPS_LONGITUDE);
    const char *paltitude = mParameters.get(CameraParameters::KEY_GPS_ALTITUDE);
    const char *ptimestamp = mParameters.get(CameraParameters::KEY_GPS_TIMESTAMP);
    const char *pprocmethod = mParameters.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);

    // check whether the GIS Information is valid
    if((NULL == platitude) || (NULL == plongitude)
        || (NULL == paltitude) || (NULL == ptimestamp)
        || (NULL == pprocmethod))
        gps_en = false;

    attribute.enableGps = gps_en;
    LOG1("exifAttribute, gps_en:%d", gps_en);

    if(gps_en) {
        double latitude, longitude, altitude;
        long timestamp;
        unsigned len;
        struct tm time;

        // the version is given as 2.2.0.0, it is mandatory when GPSInfo tag is present
        const unsigned char gpsversion[4] = {0x02, 0x02, 0x00, 0x00};
        memcpy(attribute.gps_version_id, gpsversion, sizeof(gpsversion));

        // latitude, for example, 39.904214 degrees, N
        latitude = fabs(atof(platitude));
        if(atol(platitude) > 0)
            memcpy(attribute.gps_latitude_ref, "N", sizeof(attribute.gps_latitude_ref));
        else
            memcpy(attribute.gps_latitude_ref, "S", sizeof(attribute.gps_latitude_ref));
        attribute.gps_latitude[0].num = (uint32_t)latitude;
        attribute.gps_latitude[0].den = 1;
        attribute.gps_latitude[1].num = (uint32_t)((latitude - attribute.gps_latitude[0].num) * 60);
        attribute.gps_latitude[1].den = 1;
        attribute.gps_latitude[2].num = (uint32_t)(((latitude - attribute.gps_latitude[0].num) * 60 - attribute.gps_latitude[1].num) * 60 * 100);
        attribute.gps_latitude[2].den = 100;
        LOG1("exifAttribute, latitude, ref:%s, dd:%d, mm:%d, ss:%d",
            attribute.gps_latitude_ref, attribute.gps_latitude[0].num,
            attribute.gps_latitude[1].num, attribute.gps_latitude[2].num);

        // longitude, for example, 116.407413 degrees, E
        longitude = fabs(atof(plongitude));
        if(atol(plongitude) > 0)
            memcpy(attribute.gps_longitude_ref, "E", sizeof(attribute.gps_longitude_ref));
        else
            memcpy(attribute.gps_longitude_ref, "W", sizeof(attribute.gps_longitude_ref));
        attribute.gps_longitude[0].num = (uint32_t)longitude;
        attribute.gps_longitude[0].den = 1;
        attribute.gps_longitude[1].num = (uint32_t)((longitude - attribute.gps_longitude[0].num) * 60);
        attribute.gps_longitude[1].den = 1;
        attribute.gps_longitude[2].num = (uint32_t)(((longitude - attribute.gps_longitude[0].num) * 60 - attribute.gps_longitude[1].num) * 60 * 100);
        attribute.gps_longitude[2].den = 100;
        LOG1("exifAttribute, longitude, ref:%s, dd:%d, mm:%d, ss:%d",
            attribute.gps_longitude_ref, attribute.gps_longitude[0].num,
            attribute.gps_longitude[1].num, attribute.gps_longitude[2].num);

        // altitude, see level or above see level, set it to 0; below see level, set it to 1
        altitude = fabs(atof(paltitude));
        attribute.gps_altitude_ref = ((atol(paltitude) > 0) ? 0 : 1);
        attribute.gps_altitude.num = (uint32_t)altitude;
        attribute.gps_altitude.den = 1;
        LOG1("exifAttribute, altitude, ref:%d, height:%d",
            attribute.gps_altitude_ref, attribute.gps_altitude.num);

        // timestampe
        timestamp = atol(ptimestamp);
        gmtime_r(&timestamp, &time);
        attribute.gps_timestamp[0].num = time.tm_hour;
        attribute.gps_timestamp[0].den = 1;
        attribute.gps_timestamp[1].num = time.tm_min;
        attribute.gps_timestamp[1].den = 1;
        attribute.gps_timestamp[2].num = time.tm_sec;
        attribute.gps_timestamp[2].den = 1;
        snprintf((char *)attribute.gps_datestamp, sizeof(attribute.gps_datestamp), "%04d:%02d:%02d",
            time.tm_year, time.tm_mon, time.tm_mday);
        LOG1("exifAttribute, timestamp, year:%d,mon:%d,day:%d,hour:%d,min:%d,sec:%d",
            time.tm_year, time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);

        // processing method
        if(strlen(pprocmethod) + 1 >= sizeof(attribute.gps_processing_method))
            len = sizeof(attribute.gps_processing_method);
        else
            len = strlen(pprocmethod) + 1;
        memcpy(attribute.gps_processing_method, pprocmethod, len);
        LOG1("exifAttribute, proc method:%s", attribute.gps_processing_method);
    }
}

// handle the exif tags data
void CameraHardware::exifAttribute(exif_attribute_t& attribute, int cap_w, int cap_h, bool thumbnail_en)
{
    int ae_mode;

    memset(&attribute, 0, sizeof(attribute));

    attribute.enableThumb = thumbnail_en;
    LOG1("exifAttribute, thumbnal:%d", thumbnail_en);

    // make image
    memcpy(attribute.image_description, EXIF_DEF_IMAGE_DESCRIPTION, sizeof(EXIF_DEF_IMAGE_DESCRIPTION) - 1);
    attribute.image_description[sizeof(EXIF_DEF_IMAGE_DESCRIPTION) - 1] = '\0';

    // maker information
    memcpy(attribute.maker, EXIF_DEF_MAKER, sizeof(EXIF_DEF_MAKER) - 1);
    attribute.maker[sizeof(EXIF_DEF_MAKER) - 1] = '\0';

    // module information
    memcpy(attribute.model, EXIF_DEF_MODEL, sizeof(EXIF_DEF_MODEL) -1);
    attribute.model[sizeof(EXIF_DEF_MODEL) - 1] = '\0';

    // software information
    memcpy(attribute.software, EXIF_DEF_SOFTWARE, sizeof(EXIF_DEF_SOFTWARE) - 1);
    attribute.software[sizeof(EXIF_DEF_SOFTWARE) - 1] = '\0';

    // exif version, it is fixed with 0220
    memcpy(attribute.exif_version, EXIF_DEF_EXIF_VERSION, sizeof(attribute.exif_version));

    // time information
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime((char *)attribute.date_time, sizeof(attribute.date_time), "%Y:%m:%d %H:%M:%S", timeinfo);

    // exposure time
    float exp_time;
    mAAA->AeGetMode (&ae_mode);
    if(ae_mode == CAM_AE_MODE_MANUAL || ae_mode == CAM_AE_MODE_APERTURE_PRIORITY) {
        mAAA->AeGetManualShutter(&exp_time);
        attribute.exposure_time.num = (uint32_t)(exp_time * 100);
        attribute.exposure_time.den = 100;
    } else {    // TBD
        attribute.exposure_time.num = 0;
        attribute.exposure_time.den = 1;
    }

    // shutter speed
    float shutter;
    mAAA->AeGetMode (&ae_mode);
    if(ae_mode == CAM_AE_MODE_MANUAL || ae_mode == CAM_AE_MODE_APERTURE_PRIORITY) {
        mAAA->AeGetManualShutter(&shutter);
    } else {    // TBD
        attribute.shutter_speed.num = 0;
        attribute.shutter_speed.den = 1;
    }

    // aperture
    float aperture;
    if(ae_mode == CAM_AE_MODE_MANUAL || ae_mode == CAM_AE_MODE_APERTURE_PRIORITY) {
        mAAA->AeGetManualAperture(&aperture);
        attribute.aperture.num = (uint32_t)(aperture * 100);
        attribute.aperture.den = 100;
    } else {    // TBD
        attribute.aperture.num = 0;
        attribute.aperture.den = 1;
    }

    // fnumber
    attribute.fnumber.num = EXIF_DEF_FNUMBER_NUM;  // TBD
    attribute.fnumber.den = EXIF_DEF_FNUMBER_DEN;

    // conponents configuration. 0 means does not exist
    // 1 = Y; 2 = Cb; 3 = Cr; 4 = R; 5 = G; 6 = B; other = reserved
    memset(attribute.components_configuration, 0, sizeof(attribute.components_configuration));

    // brightness, -99.99 to 99.99. FFFFFFFF.H means unknown.
    attribute.brightness.num = (~0);
    attribute.brightness.den = 1;

    // exposure bias. unit is APEX value. -99.99 to 99.99
    float bias;
    mAAA->AeGetEv(&bias);
    attribute.exposure_bias.num = (int)(bias * 100);
    attribute.exposure_bias.den = 100;

    // max aperture. the smallest F number of the lens. unit is APEX value.
    attribute.max_aperture.num = 0; // TBD
    attribute.max_aperture.den = 1;

    // subject distance,    0 means distance unknown; (~0) means infinity.
    attribute.subject_distance.num = EXIF_DEF_SUBJECT_DISTANCE_UNKNOWN;
    attribute.subject_distance.den = 1;

    // flashpix version
    memcpy(attribute.flashpix_version, EXIF_DEF_FLASHPIXVERSION, sizeof(attribute.flashpix_version));

    // light source, 0 means light source unknown
    attribute.light_source = 0;

    // gain control, 0 = none;
    // 1 = low gain up; 2 = high gain up; 3 = low gain down; 4 = high gain down
    attribute.gain_control = 0;

    // sharpness, 0 = normal; 1 = soft; 2 = hard; other = reserved
    attribute.sharpness = 0;

    // comment information
    memcpy(attribute.user_comment, EXIF_DEF_USERCOMMENTS, sizeof(EXIF_DEF_USERCOMMENTS) - 1);
    attribute.user_comment[sizeof(EXIF_DEF_USERCOMMENTS) - 1] = '\0';

    // the picture's width and height
    attribute.width = cap_w;
    attribute.height = cap_h;

    // we use the postview for the thumbnail src
    attribute.widthThumb = mPostViewWidth;
    attribute.heightThumb = mPostViewHeight;
    LOG1("exifAttribute, mPostViewWidth:%d, mPostViewHeight:%d",
        mPostViewWidth, mPostViewHeight);

    exifAttributeOrientation(attribute);

    // the TIFF default is 1 (centered)
    attribute.ycbcr_positioning = EXIF_DEF_YCBCR_POSITIONING;

    // set the exposure program mode
    int aemode;
    if (AAA_SUCCESS == mAAA->AeGetMode(&aemode)) {
        switch (aemode) {
            case CAM_AE_MODE_MANUAL:
                attribute.exposure_program = EXIF_EXPOSURE_PROGRAM_MANUAL;
                break;
            case CAM_AE_MODE_SHUTTER_PRIORITY:
                attribute.exposure_program = EXIF_EXPOSURE_PROGRAM_SHUTTER_PRIORITY;
                break;
            case CAM_AE_MODE_APERTURE_PRIORITY:
                attribute.exposure_program = EXIF_EXPOSURE_PROGRAM_APERTURE_PRIORITY;
                break;
            case CAM_AE_MODE_AUTO:
            default:
                attribute.exposure_program = EXIF_EXPOSURE_PROGRAM_NORMAL;
                break;
        }
    } else {
        attribute.exposure_program = EXIF_EXPOSURE_PROGRAM_NORMAL;
    }

    // indicates the ISO speed of the camera
    int sensitivity;
    if (AAA_SUCCESS == mAAA->AeGetManualIso(&sensitivity)) {
        attribute.iso_speed_rating = sensitivity;
    } else {
        attribute.iso_speed_rating = 100;
    }

    // the metering mode.
    int meteringmode;
    if (AAA_SUCCESS == mAAA->AeGetMeteringMode(&meteringmode)) {
        switch (meteringmode) {
                case CAM_AE_METERING_MODE_AUTO:
                    attribute.metering_mode = EXIF_METERING_AVERAGE;
                    break;
                case CAM_AE_METERING_MODE_SPOT:
                    attribute.metering_mode = EXIF_METERING_SPOT;
                    break;
                case CAM_AE_METERING_MODE_CENTER:
                    attribute.metering_mode = EXIF_METERING_CENTER;
                    break;
                case CAM_AE_METERING_MODE_CUSTOMIZED:
                default:
                    attribute.metering_mode = EXIF_METERING_OTHER;
                    break;
            }
    } else {
        attribute.metering_mode = EXIF_METERING_OTHER;
    }

    // bit 0: flash fired; bit 1 to 2: flash return; bit 3 to 4: flash mode;
    // bit 5: flash function; bit 6: red-eye mode;
    int mode;
    if (AAA_SUCCESS == mAAA->AeGetFlashMode(&mode)) {
        if (mode == CAM_AE_FLASH_MODE_ON)
            attribute.flash = EXIF_FLASH_ON;
        else
            attribute.flash = EXIF_DEF_FLASH;
    } else {
            attribute.flash = EXIF_DEF_FLASH;
    }

    // normally it is sRGB, 1 means sRGB. FFFF.H means uncalibrated
    attribute.color_space = EXIF_DEF_COLOR_SPACE;

    // exposure mode settting. 0: auto; 1: manual; 2: auto bracket; other: reserved
    if (AAA_SUCCESS == mAAA->AeGetMode(&ae_mode)) {
        switch (ae_mode) {
            case CAM_AE_MODE_MANUAL:
                attribute.exposure_mode = EXIF_EXPOSURE_MANUAL;
                break;
            default:
                attribute.exposure_mode = EXIF_EXPOSURE_AUTO;
                break;
        }
    } else {
        attribute.exposure_mode = EXIF_EXPOSURE_AUTO;
    }

    // white balance mode. 0: auto; 1: manual
    int awbmode;
    if(AAA_SUCCESS == mAAA->AwbGetMode(&awbmode)) {
        switch (awbmode) {
            case CAM_AWB_MODE_AUTO:
                attribute.white_balance = EXIF_WB_AUTO;
                break;
            default:
                attribute.white_balance = EXIF_WB_MANUAL;
                break;
        }
    } else {
        attribute.white_balance = EXIF_WB_AUTO;
    }

    // scene mode
    int scenemode;
    if (AAA_SUCCESS == mAAA->AeGetSceneMode(&scenemode)) {
        switch (scenemode) {
            case CAM_AE_SCENE_MODE_PORTRAIT:
                attribute.scene_capture_type = EXIF_SCENE_PORTRAIT;
                break;
            case CAM_AE_SCENE_MODE_LANDSCAPE:
                attribute.scene_capture_type = EXIF_SCENE_LANDSCAPE;
                break;
            case CAM_AE_SCENE_MODE_NIGHT:
                attribute.scene_capture_type = EXIF_SCENE_NIGHT;
                break;
            default:
                attribute.scene_capture_type = EXIF_SCENE_STANDARD;
                break;
        }
    } else {
        attribute.scene_capture_type = EXIF_SCENE_STANDARD;
    }

    // the actual focal length of the lens, in mm.
    // there is no API for lens position.
    attribute.focal_length.num = EXIF_DEF_FOCAL_LEN_NUM;
    attribute.focal_length.den = EXIF_DEF_FOCAL_LEN_DEN;

    // GIS information
    exifAttributeGPS(attribute);

    // the number of pixels per ResolutionUnit in the w or h direction
    // 72 means the image resolution is unknown
    attribute.x_resolution.num = EXIF_DEF_RESOLUTION_NUM;
    attribute.x_resolution.den = EXIF_DEF_RESOLUTION_DEN;
    attribute.y_resolution.num = attribute.x_resolution.num;
    attribute.y_resolution.den = attribute.x_resolution.den;
    // resolution unit, 2 means inch
    attribute.resolution_unit = EXIF_DEF_RESOLUTION_UNIT;
    // when thumbnail uses JPEG compression, this tag 103H's value is set to 6
    attribute.compression_scheme = EXIF_DEF_COMPRESSION;
}

#define MAX_FRAME_WAIT 3
#define FLASH_FRAME_WAIT 4
int CameraHardware::pictureThread()
{
    LOGD("%s :start", __func__);

    int ret;
    if (use_file_input) {
        ret = mCamera->initFileInput();
        if (ret == 0) {
            ret = mCamera->configureFileInput(&mFile);
        }
        if (ret < 0)
            mCamera->deInitFileInput();
    }
    int cap_width, cap_height, cap_frame_size, rgb_frame_size;
    void *pmainimage;
    void *pthumbnail;   // first save RGB565 data, then save jpeg encoded data into this pointer
    mCamera->getPostViewSize(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    mCamera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);
    rgb_frame_size = cap_width * cap_height * 2;

    //For postview
    sp<MemoryBase> pv_buffer = new MemoryBase(mRawHeap, 0, mPostViewSize);

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        int index;
        void *main_out, *postview_out;
        postview_out = mRawHeap->getBase();
        unsigned int page_size = getpagesize();
        unsigned int capsize_aligned = (rgb_frame_size + page_size - 1)
                                              & ~(page_size - 1);
        unsigned total_size = capsize_aligned + exif_offset + thumbnail_offset;

        sp<MemoryHeapBase> picHeap = new MemoryHeapBase(total_size);
        pthumbnail = (void*)((char*)(picHeap->getBase()) + exif_offset);
        pmainimage = (void*)((char*)(picHeap->getBase()) + exif_offset + thumbnail_offset);

        if (memory_userptr) {
            mCamera->setSnapshotUserptr(0, pmainimage, mRawHeap->getBase());
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
        bool flash_status_tmp;
        mCamera->getFlashStatus(&flash_status_tmp);
        if(!flash_status_tmp)
            mCamera->setIndicatorIntensity(INDICATOR_INTENSITY_WORKING);

        //Skip the first frame
        //Don't need the flash for the skip frame
        bool flash_status;
        mCamera->getFlashStatus(&flash_status);
        mCamera->setFlashStatus(false); //turn off flash
        index = mCamera->getSnapshot(&main_out, &postview_out, NULL);
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
        index = mCamera->getSnapshot(&main_out, &postview_out, pthumbnail);
        if (index < 0)
            goto get_img_error;
        LOGD("RAW image got: size %dB", rgb_frame_size);

        if (!memory_userptr) {
            memcpy(pmainimage, main_out, rgb_frame_size);
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&second_frame, 0);
#endif

        //Postview
        if (use_texture_streaming) {
            int mPostviewId = 0;
            memcpy(mRawIdHeap->base(), &mPostviewId, sizeof(int));
            mDataCb(CAMERA_MSG_RAW_IMAGE, mRawIdBase, mCallbackCookie);
            LOGD("Sent postview frame id: %d", mPostviewId);
        } else {
            /* TODO: YUV420->RGB565 */
            mDataCb(CAMERA_MSG_RAW_IMAGE, pv_buffer, mCallbackCookie);
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&postview, 0);
#endif
        mCamera->setIndicatorIntensity(INDICATOR_INTENSITY_OFF);

        //Stop the Camera Now
        mCamera->putSnapshot(index);
        mCamera->stopSnapshot();

        //De-initialize file input
        if (use_file_input)
            mCamera->deInitFileInput();

        mCamera->SnapshotPostProcessing (main_out);

#ifdef PERFORMANCE_TUNING
        gettimeofday(&snapshot_stop, 0);
#endif
        //software encoding by skia and then copy out to the raw memory
        if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
            int exif_size;
            int thumbnail_size; // it will be included in the exif_size
            int mainimage_size;
            int jpeg_file_size;
            int main_quality, thumbnail_quality;
            unsigned tmp;
            exif_attribute_t exifattribute;
            JpegEncoder jpgenc;
            const unsigned char FILE_START[2] = {0xFF, 0xD8};
            const unsigned char FILE_END[2] = {0xFF, 0xD9};
            const char TIFF_HEADER_SIZE = 20;

            // get the Jpeg Quality setting
            main_quality = mParameters.getInt(CameraParameters::KEY_JPEG_QUALITY);
            if (-1 == main_quality) {
                main_quality = mJpegQualityDefault;
            }
            thumbnail_quality = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
            if (-1 == thumbnail_quality) {
                thumbnail_quality = mJpegQualityDefault;
            }
            LOG1("main_quality:%d, thumbnail_quality:%d", main_quality, thumbnail_quality);

            // RGB565 color space conversion
            mCamera->toRGB565(cap_width, cap_height, mPicturePixelFormat, pmainimage, pmainimage);

            // encode the main image
            if (encodeToJpeg(cap_width, cap_height, pmainimage, pmainimage, &mainimage_size, main_quality) < 0)
                goto start_error_out;

            // encode the thumbnail
            void *pdst = pthumbnail;
            if (encodeToJpeg(mPostViewWidth, mPostViewHeight, pthumbnail, pdst, &thumbnail_size, thumbnail_quality) < 0)
                goto start_error_out;
            pdst = (char*)pdst + TIFF_HEADER_SIZE - sizeof(FILE_START);
            memcpy(pdst, FILE_START, sizeof(FILE_START));
            thumbnail_size = thumbnail_size - TIFF_HEADER_SIZE + sizeof(FILE_START) - sizeof(FILE_END);

            // fill the attribute
            if (thumbnail_size >= exif_offset) {
                // thumbnail is in the exif, so the size of it must less than exif_offset
                exifAttribute(exifattribute, cap_width, cap_height, false);
            } else {
                exifAttribute(exifattribute, cap_width, cap_height, true);
            }

            // set thumbnail data pointer
            jpgenc.setThumbData((unsigned char *)pdst, thumbnail_size);

            // generate exif, it includes memcpy the thumbnail
            jpgenc.makeExif((unsigned char*)(picHeap->getBase()) + sizeof(FILE_START), &exifattribute, &tmp, 0);
            exif_size = (int)tmp;
            LOG1("exif sz:0x%x,thumbnail sz:0x%x,main sz:0x%x", exif_size, thumbnail_size, mainimage_size);

            // move data together
            void *pjpg_start = picHeap->getBase();
            void *pjpg_exifend = (void*)((char*)pjpg_start + sizeof(FILE_START) + exif_size);
            void *pjpg_main = (void*)((char*)pjpg_exifend + sizeof(FILE_END));
            void *psrc = (void*)((char*)pmainimage + TIFF_HEADER_SIZE);
            memcpy(pjpg_start, FILE_START, sizeof(FILE_START));
            memcpy(pjpg_exifend, FILE_END, sizeof(FILE_END));
            memmove(pjpg_main, psrc, mainimage_size - TIFF_HEADER_SIZE);

            jpeg_file_size =sizeof(FILE_START) + exif_size + sizeof(FILE_END) + mainimage_size - TIFF_HEADER_SIZE;
            LOG1("jpg file sz:%d", jpeg_file_size);

            sp<MemoryBase> JpegBuffer = new MemoryBase(picHeap, 0, jpeg_file_size);
            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, JpegBuffer, mCallbackCookie);
            JpegBuffer.clear();
        }
#ifdef PERFORMANCE_TUNING
        gettimeofday(&jpeg_encoded, 0);
#endif

        //clean up
        mCamera->releasePostviewBcd();
        pv_buffer.clear();
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

    if (use_file_input)
        mCamera->deInitFileInput();

start_error_out:
    mStateLock.lock();
    mCaptureInProgress = false;
    mStateLock.unlock();
    LOGE("%s :end", __func__);

    return UNKNOWN_ERROR;
}

/*
    width, height: the w and h of the pic
    psrc:source buffer, currently it must be RGB565 format
    pdst:dest buffer, we save encoded jpeg data to this pointer
    jsize:write encoded jpeg file size to this pointer.
    quality:valid value is from 0 to 100
*/
status_t CameraHardware::encodeToJpeg(int width, int height, void *psrc, void *pdst, int *jsize, int quality)
{
    SkDynamicMemoryWStream *stream = NULL;
    SkBitmap *bitmap = NULL;
    SkImageEncoder* encoder = NULL;

    stream = new SkDynamicMemoryWStream;
    if (stream == NULL) {
        LOGE("%s: No memory for stream\n", __func__);
        goto stream_error;
    }

    bitmap = new SkBitmap();
    if (bitmap == NULL) {
        LOGE("%s: No memory for bitmap\n", __func__);
        goto bitmap_error;
    }

    encoder = SkImageEncoder::Create(SkImageEncoder::kJPEG_Type);
    if (encoder != NULL) {
        bool success = false;
        bitmap->setConfig(SkBitmap::kRGB_565_Config, width, height);
        bitmap->setPixels(psrc, NULL);
        success = encoder->encodeStream(stream, *bitmap, quality);
        *jsize = stream->getOffset();
        stream->copyTo(pdst);
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
    mCamera->calculateLightLevel();
    switch(mCamera->getFlashMode())
    {
        case CAM_AE_FLASH_MODE_AUTO:
            bool flash_status;
            mCamera->getFlashStatus(&flash_status);
            if(!flash_status) break;
        case CAM_AE_FLASH_MODE_ON:
            mCamera->setAssistIntensity(ASSIST_INTENSITY_WORKING);
            break;
        default:
            break;
    }

    int af_mode;
    mAAA->AfGetMode (&af_mode);
    if (af_mode != CAM_AF_MODE_MANUAL)
    {
        af_status = mCamera->runStillAfSequence();
    }
    else
    {
        //manual focus, just return focused
        af_status = true;
    }

    mCamera->setAssistIntensity(ASSIST_INTENSITY_OFF);
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

/*
 * update 3A parameters according to settings
 * Arguments:
 *    p:    new parameters
 *    flush_only:    flase - update local parameter structure and 3A parameters according to user settings
 *                           true - update 3A parametters according to local parameter structure
 */
int  CameraHardware::update3AParameters(CameraParameters& p, bool flush_only)
{
    cam_Window win_new, win_old;
    int ret;
    const char *new_value, *set_value;

        bool ae_to_manual = false;
        bool ae_to_aperture_priority = false;
        bool ae_to_shutter_priority = false;
        bool af_to_manual = false;
        bool awb_to_manual = false;

        // ae mode
        const char * pmode = CameraParameters::KEY_AE_MODE;
        new_value = p.get(pmode);
        if (!flush_only)
        {
            set_value = mParameters.get(pmode);
            LOGD(" -ae mode = new \"%s\"  / current \"%s\"", new_value, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 ||flush_only ) {
            int ae_mode;

            if(!strcmp(new_value, "auto"))
                ae_mode = CAM_AE_MODE_AUTO;
            else if(!strcmp(new_value, "manual"))
            {
                ae_mode = CAM_AE_MODE_MANUAL;
                ae_to_manual = true;
            }
            else if(!strcmp(new_value, "shutter-priority"))
            {
                ae_mode = CAM_AE_MODE_SHUTTER_PRIORITY;
                ae_to_shutter_priority = true;
            }
            else if(!strcmp(new_value, "aperture-priority"))
            {
                ae_mode = CAM_AE_MODE_APERTURE_PRIORITY;
                ae_to_aperture_priority = true;
            }
            else
                ae_mode = CAM_AE_MODE_AUTO;
            mAAA->AeSetMode(ae_mode);

            LOGD("     ++ Changed ae mode to %s, %d\n",p.get(pmode), ae_mode);
        }

        //Focus Mode
        const char * pfocusmode = CameraParameters::KEY_FOCUS_MODE;
        int focus_mode = p.getInt(pfocusmode);
        new_value = p.get(pfocusmode);
        if (!flush_only)
        {
            set_value =mParameters.get(pfocusmode);
            LOGD(" - focus-mode = new \"%s\" (%d) / current \"%s\"", new_value, focus_mode, set_value);
        }
        else
        {
            set_value = new_value;
        }

        // handling the window information if it is in touch focus mode
        if (!strcmp(new_value, CameraParameters::FOCUS_MODE_TOUCH)) {
            char *end;

            new_value = p.get(CameraParameters::KEY_FOCUS_WINDOW);
            win_new.x_left = (int)strtol(new_value, &end, 10);
            win_new.y_top = (int)strtol(end+1, &end, 10);
            win_new.x_right = (int)strtol(end+1, &end, 10);
            win_new.y_bottom = (int)strtol(end+1, &end, 10);
            win_new.weight = 1; // for spot1

            ret = mAAA->AfSetWindow(&win_new);
            LOGD("AfSetWindow, tf, x_left:%d, y_top:%d, x_right:%d, y_bottom:%d, weight%d, result:%d",
                win_new.x_left, win_new.y_top, win_new.x_right, win_new.y_bottom, win_new.weight, ret);
        } else {
            int mode, w, h;

            mAAA->AfGetMeteringMode(&mode);
            if (CAM_AF_METERING_MODE_SPOT == mode) {
                ret = mAAA->AfGetWindow(&win_old);
                LOGD("AfGetWindow, x_left:%d, y_top:%d, x_right:%d, y_bottom:%d, weight%d, result:%d",
                    win_old.x_left, win_old.y_top, win_old.x_right, win_old.y_bottom, win_old.weight, ret);

                p.getPreviewSize(&w, &h);
                win_new.x_left = (w - 128) >> 1;    // 128 is the touch focus window's width
                win_new.y_top = (h - 96) >> 1;  // 96 is the touch focus window's height
                win_new.x_right = win_new.x_left + 128;
                win_new.y_bottom = win_new.y_top + 96;
                win_new.weight = win_old.weight;

                if (memcmp(&win_new, &win_old, sizeof(cam_Window))) {
                    ret = mAAA->AfSetWindow(&win_new);
                    LOGD("AfSetWindow, x_left:%d, y_top:%d, x_right:%d, y_bottom:%d, weight%d, result:%d",
                        win_new.x_left, win_new.y_top, win_new.x_right, win_new.y_bottom, win_new.weight, ret);
                }
            }
        }

        if (strcmp(set_value, new_value) != 0 || flush_only) {
            int afmode;

            if(!strcmp(new_value, CameraParameters::FOCUS_MODE_AUTO))
                afmode = CAM_AF_MODE_AUTO;
            else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_INFINITY))
                afmode = CAM_AF_MODE_INFINITY;
            else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_MACRO))
                afmode = CAM_AF_MODE_MACRO;
            else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_CONTINUOUS_VIDEO))
                afmode = CAM_AF_MODE_AUTO;
            else if(!strcmp(new_value, "manual"))
            {
                afmode = CAM_AF_MODE_MANUAL;
                af_to_manual = true;
            }
            else
                afmode = CAM_AF_MODE_AUTO;

            mAAA->AfSetMode(afmode);

            LOGD("     ++ Changed focus-mode to %s, afmode:%d",p.get(pfocusmode), afmode);
        }

        // white balance
        const char * pwb = CameraParameters::KEY_WHITE_BALANCE;
        int whitebalance = p.getInt(pwb);
        new_value = p.get(pwb);
        if (!flush_only)
        {
            set_value = mParameters.get(pwb);
            LOGD(" - whitebalance = new \"%s\" (%d) / current \"%s\"", new_value,
                whitebalance, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            int wb_mode;

            if(!strcmp(new_value, "auto"))
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
            else if(!strcmp(new_value, "manual"))
            {
                wb_mode = CAM_AWB_MODE_MANUAL_INPUT;
                awb_to_manual = true;
            }
            else
                wb_mode = CAM_AWB_MODE_AUTO;
            mAAA->AwbSetMode(wb_mode);

            LOGD("     ++ Changed whitebalance to %s, wb_mode:%d\n",p.get(pwb), wb_mode);
        }

        // ae metering mode
        const char * paemeteringmode = CameraParameters::KEY_AE_METERING_MODE;
        new_value = p.get(paemeteringmode);
        if (!flush_only)
        {
            set_value = mParameters.get(paemeteringmode);
            LOGD(" -ae metering mode = new \"%s\"  / current \"%s\"", new_value, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            int ae_metering_mode;

            if(!strcmp(new_value, "auto"))
                ae_metering_mode = CAM_AE_METERING_MODE_AUTO;
            else if(!strcmp(new_value, "spot"))
                ae_metering_mode = CAM_AE_METERING_MODE_SPOT;
            else if(!strcmp(new_value, "center"))
                ae_metering_mode = CAM_AE_METERING_MODE_CENTER;
            else if(!strcmp(new_value, "customized"))
                ae_metering_mode = CAM_AE_METERING_MODE_CUSTOMIZED;
            else
                ae_metering_mode = CAM_AE_METERING_MODE_AUTO;
            mAAA->AeSetMeteringMode(ae_metering_mode);

            LOGD("     ++ Changed ae metering mode to %s, %d\n",p.get(paemeteringmode), ae_metering_mode);
        }

        // af metering mode
        const char * pafmode = CameraParameters::KEY_AF_METERING_MODE;
        new_value = p.get(pafmode);
        if (!flush_only)
        {
            set_value = mParameters.get(pafmode);
            LOGD(" -af metering mode = new \"%s\"  / current \"%s\"", new_value, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            int af_metering_mode;

            if(!strcmp(new_value, "auto"))
                af_metering_mode = CAM_AF_METERING_MODE_AUTO;
            else if(!strcmp(new_value, "spot"))
                af_metering_mode = CAM_AF_METERING_MODE_SPOT;
            else
                af_metering_mode = CAM_AF_METERING_MODE_AUTO;
            mAAA->AfSetMeteringMode(af_metering_mode);

            LOGD("     ++ Changed af metering mode to %s, %d\n",p.get(pafmode), af_metering_mode);
        }

        // ae lock mode
        const char * paelock = CameraParameters::KEY_AE_LOCK_MODE;
        new_value = p.get(paelock);
        if(!flush_only)
        {
            set_value = mParameters.get(paelock);
            LOGD(" -ae lock mode = new \"%s\"  / current \"%s\"", new_value, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            bool ae_lock;

            if(!strcmp(new_value, "lock"))
                ae_lock = true;
            else if(!strcmp(new_value, "unlock"))
                ae_lock = false;
            else
                ae_lock = true;
            mAAA->AeLock(ae_lock);

            LOGD("     ++ Changed ae lock mode to %s, %d\n",p.get(paelock), ae_lock);
        }

         // backlight correction
        const char * pbkcor = CameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE;
        new_value = p.get(pbkcor);
        if (!flush_only)
        {
            set_value = mParameters.get(pbkcor);
            LOGD(" -ae backlight correction = new \"%s\"  / current \"%s\"", new_value, set_value);
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            bool backlight_correction;

            if(!strcmp(new_value, "on"))
                backlight_correction= true;
            else if(!strcmp(new_value, "off"))
                backlight_correction= false;
            else
                backlight_correction = true;
            mAAA->AeSetBacklightCorrection(backlight_correction);

            LOGD("     ++ Changed ae backlight correction to %s, %d\n",p.get(pbkcor), backlight_correction);
        }

        // redeye correction
        const char * predeye = CameraParameters::KEY_RED_EYE_MODE;
        new_value = p.get(predeye);
        if (!flush_only)
        {
            set_value = mParameters.get(predeye);
            LOGD(" -red eye correction = new \"%s\"  / current \"%s\"", new_value, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 ||flush_only) {
            bool red_eye_correction;

            if(!strcmp(new_value, "on"))
                red_eye_correction= true;
            else if(!strcmp(new_value, "off"))
                red_eye_correction= false;
            else
                red_eye_correction = true;
            mAAA->SetRedEyeRemoval(red_eye_correction);

            LOGD("     ++ Changed red eye correction to %s, %d\n",p.get(predeye), red_eye_correction);
        }

        // awb mapping mode
        const char * pawbmap = CameraParameters::KEY_AWB_MAPPING_MODE;
        new_value = p.get(pawbmap);
        if (!flush_only)
        {
            set_value = mParameters.get(pawbmap);
            LOGD(" -awb mapping = new \"%s\"  / current \"%s\"", new_value, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            int awb_mapping;

            if(!strcmp(new_value, "indoor"))
                awb_mapping = CAM_AWB_MAP_INDOOR;
            else if(!strcmp(new_value, "outdoor"))
                awb_mapping = CAM_AWB_MAP_OUTDOOR;
            else
                awb_mapping = CAM_AWB_MAP_INDOOR;
            mAAA->AwbSetMapping(awb_mapping);

            LOGD("     ++ Changed awb mapping to %s, %d\n",p.get(pawbmap), awb_mapping);
        }

        // manual color temperature
        int cur_awb_mode;
        mAAA->AwbGetMode (&cur_awb_mode);
        if (cur_awb_mode == CAM_AWB_MODE_MANUAL_INPUT)
        {
            const char * pct = CameraParameters::KEY_COLOR_TEMPERATURE;
            new_value = p.get(pct);
            if (!flush_only)
            {
                set_value = mParameters.get(pct);
                LOGD(" -color temperature = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || awb_to_manual == true) {
                int ct;

                ct = atoi(new_value);
                mAAA->AwbSetManualColorTemperature(ct, !flush_only);

                LOGD("     ++ Changed color temperature to %s, %d\n",p.get(pct), ct);
            }
        }

        // manual focus
        int cur_af_mode;
        mAAA->AfGetMode (&cur_af_mode);

        if (cur_af_mode == CAM_AF_MODE_MANUAL)
        {
            const char * pfocuspos = CameraParameters::KEY_FOCUS_DISTANCES;
            new_value = p.get(pfocuspos);
            if (!flush_only)
            {
                set_value = mParameters.get(pfocuspos);
                LOGD(" -focus position = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || af_to_manual == true) {
                float focus_pos;

                focus_pos = atof(new_value);
                mAAA->AfSetManualFocus((int)(100.0 * focus_pos), !flush_only);

                LOGD("     ++ Changed focus position to %s, %f\n",p.get(pfocuspos), focus_pos);
            }
        }

        // manual control for manual exposure
        int cur_ae_mode;
        mAAA->AeGetMode (&cur_ae_mode);

        // manual aperture
        if (cur_ae_mode == CAM_AE_MODE_MANUAL || cur_ae_mode == CAM_AE_MODE_APERTURE_PRIORITY)
        {
            const char * paperture = CameraParameters::KEY_APERTURE;
            new_value = p.get(paperture);
            if (!flush_only)
            {
                set_value = mParameters.get(paperture);
                LOGD(" -manual aperture = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || ae_to_manual == true || ae_to_aperture_priority == true) {
                float aperture;

                aperture = atof(new_value + 1);
                mAAA->AeSetManualAperture(aperture, !flush_only);

                LOGD("     ++ Changed manual aperture to %s, %f\n",p.get(paperture), aperture);
            }
        }


        // manual shutter
        if (cur_ae_mode == CAM_AE_MODE_MANUAL || cur_ae_mode == CAM_AE_MODE_SHUTTER_PRIORITY)
        {
            const char * pshutter = CameraParameters::KEY_SHUTTER;
            new_value = p.get(pshutter);
            if (!flush_only)
            {
                set_value = mParameters.get(pshutter);
                LOGD(" -manual shutter = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || ae_to_manual == true || ae_to_shutter_priority == true) {
                float shutter = 1.0 / 50.0;
                bool flag_parsed = false;
                char *ps;
                if (strchr(new_value, 's') != NULL)
                {
                    // ns: n seconds
                    shutter = atof(new_value);
                    flag_parsed = true;
                }
                else if (strchr(new_value, 'm') != NULL)
                {
                    // nm: n minutes
                    shutter = atof(new_value) * 60;
                    flag_parsed = true;
                }
                else
                {
                    // n: 1/n second
                    float tmp = atof(new_value);
                    if (tmp > 0)
                    {
                        shutter = 1.0 / atof(new_value);
                        flag_parsed = true;
                    }
                }
                if (flag_parsed)
                {
                    mAAA->AeSetManualShutter(shutter, !flush_only);
                    LOGD("     ++ Changed shutter to %s, %f\n",p.get(pshutter), shutter);
                }
            }
        }

        // manual iso
        if (cur_ae_mode == CAM_AE_MODE_MANUAL)
        {
            const char * piso = CameraParameters::KEY_ISO;
            new_value = p.get(piso);
            if (!flush_only)
            {
                set_value = mParameters.get(piso);
                LOGD(" -manual iso = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || ae_to_manual == true) {
                float iso;

                iso = atoi(new_value + 4);
                mAAA->AeSetManualIso(iso, !flush_only);

                LOGD("     ++ Changed manual iso to %s, %f\n",p.get(piso), iso);
            }
        }

        //EV_compensation
        const char * pexp = CameraParameters::KEY_EXPOSURE_COMPENSATION;
        const char * pcomp_step = CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP;
        int exposure = p.getInt(pexp);
        float comp_step = p.getFloat(pcomp_step);
        new_value = p.get(pexp);
        if (!flush_only)
        {
            set_value = mParameters.get(pexp);
            LOGD(" EV Index  = new \"%s\" (%d) / current \"%s\"",new_value, exposure, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            mAAA->AeSetEv(atoi(new_value) * comp_step);
            float ev = 0;
            mAAA->AeGetEv(&ev);
            LOGD("      ++Changed exposure effect to index %s, ev valule %f",p.get(pexp), ev);
        }

        //Flicker Mode
        const char * pantibanding = CameraParameters::KEY_ANTIBANDING;
        int antibanding = p.getInt(pantibanding);
        new_value = p.get(pantibanding);
        if (!flush_only)
        {
            set_value = mParameters.get(pantibanding);
            LOGD(" - antibanding = new \"%s\" (%d) / current \"%s\"", new_value, antibanding, set_value);
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            int bandingval;

            if(!strcmp(new_value, CameraParameters::ANTIBANDING_AUTO))
                bandingval = CAM_AE_FLICKER_MODE_AUTO;
            else if(!strcmp(new_value, CameraParameters::ANTIBANDING_50HZ))
                bandingval = CAM_AE_FLICKER_MODE_50HZ;
            else if(!strcmp(new_value, CameraParameters::ANTIBANDING_60HZ))
                bandingval = CAM_AE_FLICKER_MODE_60HZ;
            else if(!strcmp(new_value, CameraParameters::ANTIBANDING_OFF))
                bandingval = CAM_AE_FLICKER_MODE_OFF;
            else
                bandingval = CAM_AE_FLICKER_MODE_AUTO;
            mAAA->AeSetFlickerMode(bandingval);

            LOGD("     ++ Changed antibanding to %s, antibanding val:%d",p.get(pantibanding), bandingval);
        }

        // Scene Mode
        const char* pscenemode = CameraParameters::KEY_SCENE_MODE;
        int scene_mode = p.getInt(pscenemode);
        new_value = p.get(pscenemode);
        if (!flush_only)
        {
            set_value = mParameters.get(pscenemode);
            LOGD(" - scene-mode = new \"%s\" (%d) / current \"%s\"", new_value,
                scene_mode, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            if (!strcmp (new_value, "auto")) {
                scene_mode = CAM_AE_SCENE_MODE_AUTO;
            }
            else if (!strcmp (new_value, "portrait")) {
                scene_mode = CAM_AE_SCENE_MODE_PORTRAIT;
            }
            else if (!strcmp (new_value, "sports")) {
                scene_mode = CAM_AE_SCENE_MODE_SPORTS;
            }
            else if (!strcmp (new_value, "landscape")) {
                scene_mode = CAM_AE_SCENE_MODE_LANDSCAPE;
            }
            else if (!strcmp (new_value, "night")) {
                scene_mode = CAM_AE_SCENE_MODE_NIGHT;
            }
            else if (!strcmp (new_value, "fireworks")) {
                scene_mode = CAM_AE_SCENE_MODE_FIREWORKS;
            }
            else {
                scene_mode = CAM_AE_SCENE_MODE_AUTO;
                LOGD("     ++ Not supported scene-mode");
            }
            mAAA->AeSetSceneMode (scene_mode);
        }

        //flash mode
        int flash_mode = p.getInt("flash-mode");
        new_value = p.get("flash-mode");
        if (!flush_only)
        {
            set_value = mParameters.get("flash-mode");
            LOGD(" - flash-mode = new \"%s\" (%d) / current \"%s\"", new_value, flash_mode, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            if (!strcmp (new_value, "auto")) {
                flash_mode = CAM_AE_FLASH_MODE_AUTO;
            }
            else if (!strcmp (new_value, "off")) {
                flash_mode = CAM_AE_FLASH_MODE_OFF;
            }
            else if (!strcmp (new_value, "on")) {
                flash_mode = CAM_AE_FLASH_MODE_ON;
            }
            else if (!strcmp (new_value, "slow-sync")) {
                flash_mode = CAM_AE_FLASH_MODE_SLOW_SYNC;
            }
            else if (!strcmp (new_value, "day-sync")) {
                flash_mode = CAM_AE_FLASH_MODE_DAY_SYNC;
            }
            else if (!strcmp (new_value, "torch")) {
               flash_mode = CAM_AE_FLASH_MODE_TORCH;
            }
            else {
                flash_mode = CAM_AE_FLASH_MODE_AUTO;
                LOGD("     ++ Not supported flash-mode");
            }
            mCamera->setFlashMode(flash_mode);
            mAAA->AeSetFlashMode (flash_mode);
        }

    mFlush3A = false;

    return 0;
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
        mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
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
    int pre_width, pre_height, pre_size, pre_padded_size, rec_w, rec_h;
    mCamera->getPreviewSize(&pre_width, &pre_height, &pre_size, &pre_padded_size);
    p.getRecordingSize(&rec_w, &rec_h);
    if(checkRecording(rec_w, rec_h)) {
        LOGD("line:%d, before setRecorderSize. w:%d, h:%d, format:%d", __LINE__, rec_w, rec_h, mVideoFormat);
        mCamera->setRecorderSize(rec_w, rec_h, mVideoFormat);
    }
    else {
        LOGD("line:%d, before setRecorderSize. w:%d, h:%d, format:%d", __LINE__, pre_width, pre_height, mVideoFormat);
        mCamera->setRecorderSize(pre_width, pre_height, mVideoFormat);
    }

    //touch Focus (focus windows)
    int x_left, x_right, y_top, y_bottom;
    x_left = p.getInt("touchfocus-x-left");
    x_right = p.getInt("touchfocus-x-right");
    y_top = p.getInt("touchfocus-x-top");
    y_bottom = p.getInt("touchfocus-x-bottom");

    // update 3A parameters to mParameters and 3A inside
    update3AParameters(p, mFlush3A);

    setISPParameters(p,mParameters);

    //Update the parameters
    mParameters = p;
    return NO_ERROR;
}
/*  this function will compare 2 parameters.
 *  parameters will be set to isp if
 *  new and current parameter diffs.
 */
int CameraHardware::setISPParameters(
                const CameraParameters &new_params,
                    const CameraParameters &old_params)
{
    const char *new_value, *set_value;
    int ret,ret2;

    ret = ret2 = -1;

    //process zoom
    int zoom = new_params.getInt("zoom");
    mCamera->set_zoom_val(zoom);

    // Color Effect
	int effect = old_params.getInt("effect");
	new_value = new_params.get("effect");
	set_value = old_params.get("effect");
	LOGD(" - effect = new \"%s\" (%d) / current \"%s\"",new_value, effect, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if(!strcmp(new_value, CameraParameters::EFFECT_MONO))
            effect = V4L2_COLORFX_BW;
        else if(!strcmp(new_value, CameraParameters::EFFECT_NEGATIVE))
            effect = V4L2_COLORFX_NEGATIVE;
        else if(!strcmp(new_value, CameraParameters::EFFECT_SEPIA))
            effect = V4L2_COLORFX_SEPIA;
        else
            effect = V4L2_COLORFX_NONE;

        ret = mCamera->setColorEffect(effect);
        if (!ret) {
            LOGD("Changed effect to %s", new_params.get("effect"));
        }
    }

    // xnr
    int xnr = old_params.getInt("xnr");
    new_value = new_params.get("xnr");
    set_value = old_params.get("xnr");
    LOGD(" - xnr = new \"%s\" (%d) / current \"%s\"",new_value, xnr, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp(new_value, "false"))
            ret = mCamera->setXNR(false);
        else if (!strcmp(new_value, "true"))
            ret = mCamera->setXNR(true);
        if (!ret) {
            LOGD("Changed xnr to %s", new_params.get("xnr"));
        }
    }

    // tnr
    int tnr = old_params.getInt("temporal-noise-reduction");
    new_value = new_params.get("temporal-noise-reduction");
    set_value = old_params.get("temporal-noise-reduction");
    LOGD(" - temporal-noise-reduction = new \"%s\" (%d) / current \"%s\"",new_value, tnr, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp(new_value, "on"))
            ret = mCamera->setTNR(true);
        else if (!strcmp(new_value, "off"))
            ret = mCamera->setTNR(false);
        if (!ret) {
            LOGD("Changed temporal-noise-reduction to %s",
                    new_params.get("temporal-noise-reduction"));
        }
    }

    // nr and ee
    int nr_ee = old_params.getInt("noise-reduction-and-edge-enhancement");
    new_value = new_params.get("noise-reduction-and-edge-enhancement");
    set_value = old_params.get("noise-reduction-and-edge-enhancement");
    LOGD(" -  noise-reduction-and-edge-enhancement= new \"%s\" (%d) / current \"%s\"",new_value, nr_ee, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp(new_value, "on")) {
            ret = mCamera->setNREE(true);
        }
        else if (!strcmp(new_value, "off")) {
            ret = mCamera->setNREE(false);
        }
        if (!ret) {
            LOGD("Changed  noise-reduction-and-edge-enhancement to %s",
                    new_params.get("noise-reduction-and-edge-enhancement"));
        }
    }

    //macc
    int color = 6;
    int macc = old_params.getInt("multi-access-color-correction");
    new_value = new_params.get("multi-access-color-correction");
    set_value = old_params.get("multi-access-color-correction");
    LOGD(" - multi-access-color-correction = new \"%s\" (%d) / current \"%s\"",new_value, macc, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp("enhance-none", new_value))
            color = V4L2_COLORFX_NONE;
        else if (!strcmp("enhance-sky", new_value))
            color = V4L2_COLORFX_SKY_BLUE;
        else if (!strcmp("enhance-grass", new_value))
            color = V4L2_COLORFX_GRASS_GREEN;
        else if (!strcmp("enhance-skin", new_value))
            color = V4L2_COLORFX_SKIN_WHITEN;
        ret = mCamera->setMACC(color);

        if (!ret) {
            LOGD("Changed multi-access-color-correction to %s",
                    new_params.get("multi-access-color-correction"));
        }
    }
    return 0;
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

/* File input interfaces */
status_t CameraHardware::setFileInputMode(int enable)
{
    use_file_input = enable;

    return NO_ERROR;
}

status_t CameraHardware::configureFileInput(char *file_name, int width, int height,
                                            int format, int bayer_order)
{
    LOGD("%s\n", __func__);
    status_t ret = -1;

    if (!use_file_input) {
        LOGE("%s: File input mode is disabled\n", __func__);
        return ret;
    }

    mFile.name = file_name;
    mFile.width = width;
    mFile.height = height;
    mFile.format = format;
    mFile.bayer_order = bayer_order;

    return 0;
}

//----------------------------------------------------------------------------
//----------------------------HAL--used for camera service--------------------
static int HAL_cameraType[MAX_CAMERAS];
static CameraInfo HAL_cameraInfo[MAX_CAMERAS] = {
    {
        CAMERA_FACING_BACK,
        270,  /* default orientation, we will modify it at other place, ToDo */
    },
    {
        CAMERA_FACING_FRONT,
        270,  /* default orientation, we will modify it at other place, ToDo */
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
