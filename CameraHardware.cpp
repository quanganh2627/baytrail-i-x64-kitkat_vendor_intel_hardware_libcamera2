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

namespace android {

#define CAMERA_MSG_TOUCH_TO_FOCUS 0x200

static const int INITIAL_SKIP_FRAME = 4;
static const int CAPTURE_SKIP_FRAME = 1;

static const int ZOOM_FACTOR = 4;

static cameraInfo camera_info[MAX_CAMERAS];
static int num_camera = 0;
static int primary_camera_id = 0;
static int secondary_camera_id = 1;

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

    setupPlatformType();
    atom_sensor_type = checkSensorType(cameraId);

    if (atom_sensor_type == ci_adv_sensor_soc)
        mSensorType = SENSOR_TYPE_SOC;
    else
        mSensorType = SENSOR_TYPE_RAW;

    cameraId = (cameraId == 0) ? primary_camera_id : secondary_camera_id;

    //Create the 3A object
    mAAA = new AAAProcess(mSensorType);

    if (cameraId == primary_camera_id)
        ret = mCamera->initCamera(CAMERA_ID_BACK, primary_camera_id, mAAA);
    else
        ret = mCamera->initCamera(CAMERA_ID_FRONT, secondary_camera_id, mAAA);

    if (ret < 0) {
        LOGE("ERR(%s):Fail on mCamera init", __func__);
    }


    LOGD("%s sensor\n", (mSensorType == SENSOR_TYPE_SOC) ?
         "SOC" : "RAW");

    initDefaultParameters();
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
    mPicturePixelFormat = V4L2_PIX_FMT_NV12;
#else
    mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
#endif
    mVideoPreviewEnabled = false;
    mFlashNecessary = false;
    mDVSProcessing = false;

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
    mCompressThread = new CompressThread(this);

    LOGD("%s: sensor is %d", __func__, atom_sensor_type);
    // init 3A for RAW sensor only
    if (mSensorType != SENSOR_TYPE_SOC) {
        mAAA->Init(atom_sensor_type);
        mAAA->SetAfEnabled(true);
        mAAA->SetAeEnabled(true);
        mAAA->SetAwbEnabled(true);
    }

    // burst capture initialization
    if ((ret = sem_init(&sem_bc_captured, 0, 0)) < 0)
        LOGE("BC, line:%d, sem_init fail, ret:%d", __LINE__, ret);
    if ((ret = sem_init(&sem_bc_encoded, 0, 0)) < 0)
        LOGE("BC, line:%d, sem_init fail, ret:%d", __LINE__, ret);
    burstCaptureInit();

#if ENABLE_BUFFER_SHARE_MODE
    isVideoStarted = false;
    isCameraTurnOffBufferSharingMode = false;
#endif
    LOGD("libcamera version: 2011-06-02 1.0.1");
}

CameraHardware::~CameraHardware()
{
    LOGI("%s: Delete the CameraHardware\n", __func__);
    int ret;

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

    if ((ret = sem_destroy(&sem_bc_captured)) < 0)
        LOGE("BC, line:%d, sem_destroy fail, ret:%d", __LINE__, ret);
    if ((ret = sem_destroy(&sem_bc_encoded)) < 0)
        LOGE("BC, line:%d, sem_destroy fail, ret:%d", __LINE__, ret);

    mAAA->Uninit();
    delete mAAA;
    mCamera->deinitCamera();
    mCamera = NULL;
    singleton.clear();
}

void CameraHardware::initDefaultParameters()
{
    CameraParameters p;
    int resolution_index;

    p.setPreviewSize(640, 480);
    if (use_texture_streaming)
        p.setPreviewFrameRate(30);
    else
        p.setPreviewFrameRate(15);

    if (mSensorType == SENSOR_TYPE_SOC)
        p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV422I);
    else
        p.setPreviewFormat(CameraParameters::PIXEL_FORMAT_YUV420SP);

    p.setPictureFormat(CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, "yuv420sp,rgb565,yuv422i-yuyv");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, "640x480,640x360");
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, "jpeg");

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
    // manual iso control
    p.set(CameraParameters::KEY_ISO, "iso-200");
    p.set(CameraParameters::KEY_SUPPORTED_ISO, "iso-100,iso-200,iso-400,iso-800,iso-1600");
    // manual color temperature
    p.set(CameraParameters::KEY_COLOR_TEMPERATURE, "5000");
    // manual focus
    p.set(CameraParameters::KEY_FOCUS_DISTANCES, "2,2,2");
    // focus window
    p.set("focus-window", "0,0,0,0");

    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
    p.set(CameraParameters::KEY_MAX_ZOOM, "60");
    p.set(CameraParameters::KEY_ZOOM_RATIOS, "100,125,150,175,200,225,250,275,300,325,350,375,400,425,450,475,500,525,"
          "550,575,600,625,650,675,700,725,750,775,800,825,850,875,900,925,950,975,1000,1025,1050,1075,1100,"
          "1125,1150,1175,1200,1225,1250,1275,1300,1325,1350,1375,1400,1425,1450,1475,1500,1525,1550,1575,1600");
    p.set(CameraParameters::KEY_ZOOM, 0);

    p.set(CameraParameters::KEY_EFFECT, "none");
    //p.set("effect-values","none,mono,negative,sepia");
    p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, "none,mono,negative,sepia");
    p.set(CameraParameters::KEY_XNR, "false");
    p.set(CameraParameters::KEY_SUPPORTED_XNR, "true,false");
    p.set(CameraParameters::KEY_GDC, "false");
    p.set(CameraParameters::KEY_SUPPORTED_GDC, "true,false");
    p.set(CameraParameters::KEY_DVS, "false");
    p.set(CameraParameters::KEY_SUPPORTED_DVS, "true,false");
    p.set(CameraParameters::KEY_DIGITAL_IMAGE_STABILIZATION, "off");
    p.set(CameraParameters::KEY_SUPPORTED_DIGITAL_IMAGE_STABILIZATION, "on,off");
    p.set(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION, "off");
    p.set(CameraParameters::KEY_SUPPORTED_TEMPORAL_NOISE_REDUCTION, "on,off");
#ifdef TUNING_EDGE_ENHACNMENT
    p.set(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT, "on");
    p.set(CameraParameters::KEY_SUPPORTED_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT, "on,off");
#endif
    p.set(CameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION, "enhance-none");
    p.set(CameraParameters::KEY_SUPPORTED_MULTI_ACCESS_COLOR_CORRECTIONS,
          "enhance-sky,enhance-grass,enhance-skin,enhance-none");

    resolution_index = mCamera->getMaxSnapShotResolution();
    switch (resolution_index) {
    case RESOLUTION_14MP:
        p.set("picture-size-values", RESOLUTION_14MP_TABLE);
        break;
    case RESOLUTION_8MP:
        p.set("picture-size-values", RESOLUTION_8MP_TABLE);
        break;
    case RESOLUTION_5MP:
        p.set("picture-size-values", RESOLUTION_5MP_TABLE);
        break;
    case RESOLUTION_1080P:
        p.set("picture-size-values", RESOLUTION_1080P_TABLE);
        break;
    case RESOLUTION_720P:
        p.set("picture-size-values", RESOLUTION_720P_TABLE);
        break;
    default:
        break;
    }

    if (mCameraId == CAMERA_FACING_BACK) {
        // For main back camera
        // flash mode option
        p.set(CameraParameters::KEY_FLASH_MODE,"off");
        p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"auto,off,on,torch,slow-sync,day-sync");
    } else {
        // For front camera
        // No flash present
        p.set(CameraParameters::KEY_FLASH_MODE,"none");
        p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"none");
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
    mPreviewLock.lock();
    //Checke whether the preview is running
    if (!mPreviewRunning) {
        mPreviewLock.unlock();
        return 0;
    }
    int index = mCamera->getPreview(&data);
    mPreviewLock.unlock();

    if (index < 0) {
        LOGE("ERR(%s):Fail on mCamera->getPreview()", __func__);
        return -1;
    }

    //Run 3A after each frame
    mPreviewFrameCondition.signal();

    //Skip the first several frames from the sensor
    if (mSkipFrame > 0) {
        mSkipFrame--;
        mPreviewLock.lock();
        mCamera->putPreview(index);
        mPreviewLock.unlock();
        return NO_ERROR;
    }
    processPreviewFrame(data);

    //Qbuf
    mPreviewLock.lock();
    mCamera->putPreview(index);
    mPreviewLock.unlock();

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

    mPreviewLock.lock();
    int index = mCamera->getRecording(&main_out, &preview_out);
    mPreviewLock.unlock();
    if (index < 0) {
        LOGE("ERR(%s):Fail on mCamera->getRecording()", __func__);
        return -1;
    }
    //Run 3A after each frame
    mPreviewFrameCondition.signal();

    //Skip the first several frames from the sensor
    if (mSkipFrame > 0) {
        mSkipFrame--;
        mPreviewLock.lock();
        mCamera->putRecording(index);
        mPreviewLock.unlock();
        return NO_ERROR;
    }

    //Process the preview frame first
    processPreviewFrame(preview_out);

    //Process the recording frame when recording started
    if (mRecordRunning && bufferIsReady)
        processRecordingFrame(main_out, index);
    mPreviewLock.lock();
    mCamera->putRecording(index);
    mPreviewLock.unlock();
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

        mAeAfAwbLock.lock();
        mPreviewFrameCondition.wait(mAeAfAwbLock);
        LOG2("%s: 3A return from wait", __func__);
        mAeAfAwbLock.unlock();
        if (mSensorType != SENSOR_TYPE_SOC) {
            mAAA->AeAfAwbProcess(true);
            LOG2("%s: After run 3A thread", __func__);
        }
    }
}

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
    int fd;
#ifdef PERFORMANCE_TUNING
    gettimeofday(&preview_start, 0);
    print_snapshot_time();
#endif
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
    mAAA->SetAeEnabled (true);
    mAAA->SetAfEnabled(true);
    mAAA->SetAwbEnabled(true);
    mPreviewAeAfAwbCondition.signal();

    //Determine which preview we are in
    if (mVideoPreviewEnabled) {
        int w, h, size, padded_size;
        LOGD("Start recording preview\n");
        mRecordingFrame = 0;
        mPostRecordingFrame = 0;
        mCamera->getRecorderSize(&w, &h, &size, &padded_size);
        initRecordingBuffer(size, padded_size);
        fd = mCamera->startCameraRecording();
    } else {
        LOGD("Start normal preview\n");
        int w, h, size, padded_size;
        mPreviewFrame = 0;
        mPostPreviewFrame = 0;
        mCamera->getPreviewSize(&w, &h, &size, &padded_size);
        initPreviewBuffer(padded_size);
        fd = mCamera->startCameraPreview();
    }
    if (fd < 0) {
        mPreviewRunning = false;
        mPreviewLock.unlock();
        mPreviewCondition.signal();
        LOGE("ERR(%s):Fail on mCamera->startPreview()", __func__);
        return -1;
    }

    mAAA->FlushManualSettings ();

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
        mAAA->SetAeEnabled(false);
        mAAA->SetAfEnabled(false);
        mAAA->SetAwbEnabled(false);
        mPreviewFrameCondition.signal();
        mAeAfAwbEndCondition.wait(mAeAfAwbLock);
    }
    mAeAfAwbLock.unlock();

    LOGD("Stopped the 3A now\n");
    //Tell preview to stop
    mPreviewRunning = false;

    mPreviewLock.lock();
    if(mVideoPreviewEnabled) {
        mCamera->stopCameraRecording();
        deInitRecordingBuffer();
    } else {
        mCamera->stopCameraPreview();
    }
    mAAA->IspSetFd(-1);
    mPreviewLock.unlock();
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
    mExitAutoFocusThread = false;
    mAutoFocusThread->run("CameraAutoFocusThread", PRIORITY_DEFAULT);
    return NO_ERROR;
}

status_t CameraHardware::cancelAutoFocus()
{
    LOG1("%s :", __func__);
    mExitAutoFocusThread = true;

    //Wake up the auofocus thread
    mAeAfAwbEndCondition.signal();
    mPreviewFrameCondition.signal();

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
#ifdef MFLD_CDK
            attribute.orientation = 8;
#else
            attribute.orientation = 6;
#endif
        else if (180 == rotation)
            attribute.orientation = 3;
        else if (270 == rotation)
#ifdef MFLD_CDK
            attribute.orientation = 6;
#else
            attribute.orientation = 8;
#endif
    } else if (CAMERA_FACING_FRONT == mCameraId) { // sub sensor
        if (0 == rotation)
            attribute.orientation = 1;
        else if (90 == rotation)
#ifdef MFLD_CDK
            attribute.orientation = 6;
#else
            attribute.orientation = 8;
#endif
        else if (180 == rotation)
            attribute.orientation = 3;
        else if (270 == rotation)
#ifdef MFLD_CDK
            attribute.orientation = 8;
#else
            attribute.orientation = 6;
#endif
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
void CameraHardware::exifAttribute(exif_attribute_t& attribute, int cap_w, int cap_h,
                                                                            bool thumbnail_en, bool flash_en)
{
    int ae_mode;
    unsigned short exp_time, iso_speed, ss_exp_time, ss_iso_speed, aperture;
    int ret;
    unsigned int focal_length, fnumber;

    memset(&attribute, 0, sizeof(attribute));
    // exp_time's unit is 100us
    mAAA->AeGetExpCfg(&exp_time, &iso_speed, &ss_exp_time, &ss_iso_speed, &aperture);
    LOG1("exifAttribute, exptime:%d, isospeed:%d, ssexptime:%d, ssisospeed:%d, aperture:%d",
                exp_time, iso_speed, ss_exp_time, ss_iso_speed, aperture);

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
    attribute.exposure_time.num = ss_exp_time;
    attribute.exposure_time.den = 10000;

    // shutter speed, = -log2(exposure time)
    float exp_t = (float)(ss_exp_time / 10000.0);
    float shutter = -1.0 * (log10(exp_t) / log10(2.0));
    attribute.shutter_speed.num = (shutter * 10000);
    attribute.shutter_speed.den = 10000;

    // fnumber
    // TBD, should get from driver
    ret = mCamera->getFnumber(&fnumber);
    if (ret < 0) {
               /*Error handler: if driver does not support Fnumber achieving,
                        just give the default value.*/
        attribute.fnumber.num = EXIF_DEF_FNUMBER_NUM;
        attribute.fnumber.den = EXIF_DEF_FNUMBER_DEN;
        ret = 0;
    } else {
        attribute.fnumber.num = fnumber >> 16;
        attribute.fnumber.den = fnumber & 0xffff;
        LOG1("%s: fnumber:%x, num: %d, den: %d", __func__, fnumber, attribute.fnumber.num, attribute.fnumber.den);
    }

    // aperture
    attribute.aperture.num = (int)((((double)attribute.fnumber.num/(double)attribute.fnumber.den)* sqrt(100.0/aperture))*100);
    attribute.aperture.den = 100;

    // conponents configuration. 0 means does not exist
    // 1 = Y; 2 = Cb; 3 = Cr; 4 = R; 5 = G; 6 = B; other = reserved
    memset(attribute.components_configuration, 0, sizeof(attribute.components_configuration));

    // brightness, -99.99 to 99.99. FFFFFFFF.H means unknown.
    float brightness;
    mAAA->AeGetManualBrightness(&brightness);
    attribute.brightness.num = (int)(brightness*100);
    attribute.brightness.den = 100;

    // exposure bias. unit is APEX value. -99.99 to 99.99
    float bias;
    mAAA->AeGetEv(&bias);
    attribute.exposure_bias.num = (int)(bias * 100);
    attribute.exposure_bias.den = 100;
    LOG1("exifAttribute, brightness:%f, ev:%f", brightness, bias);

    // max aperture. the smallest F number of the lens. unit is APEX value.
    // TBD, should get from driver
    attribute.max_aperture.num = attribute.aperture.num;
    attribute.max_aperture.den = attribute.aperture.den;

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
        LOG1("exifAttribute AeGetManualIso fail");
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
    attribute.flash = (flash_en ? EXIF_FLASH_ON : EXIF_DEF_FLASH);

    // normally it is sRGB, 1 means sRGB. FFFF.H means uncalibrated
    attribute.color_space = EXIF_DEF_COLOR_SPACE;

    // exposure mode settting. 0: auto; 1: manual; 2: auto bracket; other: reserved
    if (AAA_SUCCESS == mAAA->AeGetMode(&ae_mode)) {
        LOG1("exifAttribute, ae mode:%d success", ae_mode);
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
    ret = mCamera->getFocusLength(&focal_length);
    if (ret < 0) {
               /*Error handler: if driver does not support focal_length achieving,
                        just give the default value.*/
        attribute.focal_length.num = EXIF_DEF_FOCAL_LEN_NUM;
        attribute.focal_length.den = EXIF_DEF_FOCAL_LEN_DEN;
        ret = 0;
    } else {
        attribute.focal_length.num = focal_length >> 16;
        attribute.focal_length.den = focal_length & 0xffff;
        LOG1("%s: focal_length:%x, num: %d, den: %d", __func__, focal_length, attribute.focal_length.num, attribute.focal_length.den);
    }

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

/* Return true, the thread will loop. Return false, the thread will terminate. */
int CameraHardware::compressThread()
{
    struct BCBuffer *bcbuf;
    int cap_w, cap_h;
    int rgb_frame_size, jpeg_buf_size;
    void *pexif;
    void *pmainimage;
    void *pthumbnail;   // first save RGB565 data, then save jpeg encoded data into this pointer
    int i, j, ret;

    mCompressLock.lock();
    LOG1("BC, line:%d, before receive mCompressCondition", __LINE__);
    mCompressCondition.wait(mCompressLock);
    LOG1("BC, line:%d, received mCompressCondition", __LINE__);
    mCompressLock.unlock();

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        int exif_size = 0;
        int thumbnail_size = 0; // it will be included in the exif_size
        int mainimage_size = 0;
        int jpeg_file_size = 0;
        int main_quality, thumbnail_quality;
        unsigned tmp;
        exif_attribute_t exifattribute;
        JpegEncoder jpgenc;
        const unsigned char FILE_START[2] = {0xFF, 0xD8};
        const unsigned char FILE_END[2] = {0xFF, 0xD9};

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

        mCamera->getSnapshotSize(&cap_w, &cap_h, &rgb_frame_size);

        // loop and wait to hande all the pictures
        for (i = 0; i < mBCNumReq; i++) {
            LOG1("BC, line:%d, before sem_wait:sem_bc_captured, %d", __LINE__, i);
            if ((ret = sem_wait(&sem_bc_captured)) < 0)
                LOGE("BC, line:%d, sem_wait fail, ret:%d", __LINE__, ret);
            bcbuf = mBCBuffer;
            for (j = 0; j < mBCNumReq; j++) {
                bcbuf = mBCBuffer + j;
                if (bcbuf->sequence == i)
                    break;
            }
            if (j == mBCNumReq) {
                LOGE("BC, line:%d, error, j:%d == mBCNumReq", __LINE__, j);
                return false;
            }
            LOG1("BC, line:%d, after sem_wait:sem_bc_captured, i:%d, j:%d", __LINE__, i, j);

            pexif = bcbuf->pdst_exif;
            pthumbnail = bcbuf->pdst_thumbnail;
            pmainimage = bcbuf->pdst_main;

            // get RGB565 main data from NV12
            mCamera->toRGB565(cap_w, cap_h, mPicturePixelFormat, bcbuf->psrc, bcbuf->psrc);

            // encode the main image
            if (encodeToJpeg(cap_w, cap_h, bcbuf->psrc, pmainimage, &mainimage_size, main_quality) < 0) {
                LOGE("BC, line:%d, encodeToJpeg fail for main image", __LINE__);
            }

            // encode the thumbnail
            void *pdst = pthumbnail;
            if (encodeToJpeg(mPostViewWidth, mPostViewHeight, pthumbnail, pdst, &thumbnail_size, thumbnail_quality) < 0) {
                LOGE("BC, line:%d, encodeToJpeg fail for main image", __LINE__);
            }
            memcpy(pdst, FILE_START, sizeof(FILE_START));
            thumbnail_size = thumbnail_size - sizeof(FILE_END);

            // fill the attribute
            if ((unsigned int)thumbnail_size >= exif_offset) {
                // thumbnail is in the exif, so the size of it must less than exif_offset
                exifAttribute(exifattribute, cap_w, cap_h, false, mFlashNecessary);
            } else {
                exifAttribute(exifattribute, cap_w, cap_h, true, mFlashNecessary);
            }

            // set thumbnail data pointer
            jpgenc.setThumbData((unsigned char *)pdst, thumbnail_size);

            // generate exif, it includes memcpy the thumbnail
            jpgenc.makeExif((unsigned char*)pexif + sizeof(FILE_START), &exifattribute, &tmp, 0);
            exif_size = (int)tmp;
            LOG1("exif sz:0x%x,thumbnail sz:0x%x,main sz:0x%x", exif_size, thumbnail_size, mainimage_size);

            // move data together
            void *pjpg_start = pexif;
            void *pjpg_exifend = (void*)((char*)pjpg_start + sizeof(FILE_START) + exif_size);
            void *pjpg_main = (void*)((char*)pjpg_exifend + sizeof(FILE_END));
            void *psrc = (void*)((char*)pmainimage + sizeof(FILE_START));
            memcpy(pjpg_start, FILE_START, sizeof(FILE_START));
            memcpy(pjpg_exifend, FILE_END, sizeof(FILE_END));
            memmove(pjpg_main, psrc, mainimage_size - sizeof(FILE_START));

            jpeg_file_size =sizeof(FILE_START) + exif_size + sizeof(FILE_END) + mainimage_size - sizeof(FILE_START);
            LOG1("jpg file sz:%d", jpeg_file_size);

            bcbuf->encoded = true;
            bcbuf->jpeg_size = jpeg_file_size;

            // post sem to let the picture thread to send the jpeg pic out
            if ((ret = sem_post(&sem_bc_encoded)) < 0)
                LOGE("BC, line:%d, sem_post fail, ret:%d", __LINE__, ret);
            LOGD("BC, line:%d, encode:%d finished,, sem_post", __LINE__, i);
        }

        if (i == mBCNumReq) {
            LOGD("BC, line:%d, leave compressThread", __LINE__);
            return false;
        }
    }

    return true;
}

void CameraHardware::burstCaptureInit(void)
{
    mBCNumCur = 0;
    mBCEn = false;
    mBCNumReq = 1;
    mBCNumSkipReq = 0;
    mBCBuffer = NULL;
    mBCHeap = NULL;
}

// call from pictureThread
int CameraHardware::burstCaptureHandle(void)
{
    LOGD("BC, %s :start", __func__);
    int fd, i, index, ret;
    int cap_w, cap_h;
    int rgb_frame_size, jpeg_buf_size, total_size;
    int skipped;
    void *main_out, *postview_out;
    struct BCBuffer *bcbuf;
    sp<MemoryBase> JpegBuffer;

    // get size
    mCamera->getPostViewSize(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    mPostViewFormat = mCamera->getPostViewPixelFormat();
    mCamera->getSnapshotSize(&cap_w, &cap_h, &rgb_frame_size);
    rgb_frame_size = cap_w * cap_h * 2; // for RGB565 format
    jpeg_buf_size = cap_w * cap_h * 3 / 10; // for store the encoded jpeg file, experience value
    total_size = rgb_frame_size + exif_offset + thumbnail_offset + jpeg_buf_size;

    // the first time of calling taking picture
    if (mBCNumCur == 1) {
        //get postview's memory base
        postview_out = mRawHeap->getBase();

        // allocate memory
        mBCHeap = new MemoryHeapBase(mBCNumReq * sizeof(struct BCBuffer));
        if (mBCHeap->getHeapID() < 0) {
            LOGE("BC, line:%d, mBCHeap fail", __LINE__);
            goto BCHANDLE_ERR;
        }
        mBCBuffer = (struct BCBuffer *)mBCHeap->getBase();
        for (i = 0; i < mBCNumReq; i++) {
            // the memory part, it follows Jozef's design
            bcbuf = mBCBuffer + i;

            bcbuf->heap = new MemoryHeapBase(total_size);
            if (bcbuf->heap->getHeapID() < 0) {
                LOGE("BC, line:%d, malloc heap fail, i:%d", __LINE__, i);
                goto BCHANDLE_ERR;
            }

            bcbuf->total_size = total_size;
            bcbuf->src_size = rgb_frame_size;

            bcbuf->jpeg_size = 0;

            bcbuf->psrc = bcbuf->heap->getBase();
            bcbuf->pdst_exif = (char *)bcbuf->psrc + bcbuf->src_size;
            bcbuf->pdst_thumbnail = (char *)bcbuf->pdst_exif + exif_offset;
            bcbuf->pdst_main = (char *)bcbuf->pdst_thumbnail + thumbnail_offset;

            bcbuf->ready = false;
            bcbuf->encoded = false;
            bcbuf->sequence = ~0;
            if (memory_userptr) {
                mCamera->setSnapshotUserptr(i, bcbuf->psrc, postview_out);
            }
        }

        //Prepare for the snapshot
        if ((fd = mCamera->startSnapshot()) < 0) {
            LOGE("BC, line:%d, startSnapshot fail", __LINE__);
            goto BCHANDLE_ERR;
        }
        mAAA->IspSetFd(fd);
        if (mSensorType == SENSOR_TYPE_RAW) {
            mFramerate = mCamera->getFramerate();
            mAAA->SwitchMode(STILL_IMAGE_MODE);
        }

        //Flush 3A results
        mAAA->FlushManualSettings ();
        update3Aresults();

        //Skip the first frame
        index = mCamera->getSnapshot(&main_out, &postview_out, NULL);
        if (index < 0) {
            LOGE("BC, line:%d, getSnapshot fail", __LINE__);
            goto BCHANDLE_ERR;
        }

        if (mCamera->putSnapshot(index) < 0) {
            LOGE("BC, line:%d, putSnapshot fail", __LINE__);
            goto BCHANDLE_ERR;
        }

        for (i = 0; i < mBCNumReq; i++) {
            // dq buffer and skip request buffer
            for (skipped = 0; skipped <= mBCNumSkipReq; skipped++) {
                //dq buffer
                index = mCamera->getSnapshot(&main_out, &postview_out, NULL);
                if (index < 0) {
                    LOGE("BC, line:%d, getSnapshot fail", __LINE__);
                    goto BCHANDLE_ERR;
                }
                if (i == 0) { // we don't need to skip the first frame
                    LOG1("BC, line:%d, dq buffer, i:%d", __LINE__, i);
                    break;
                }

                if(skipped < mBCNumSkipReq) {
                    mCamera->putSnapshot(index);
                    LOG1("BC, line:%d, skipped dq buffer, i:%d", __LINE__, i);
                }
                else
                    LOG1("BC, line:%d, dq buffer, i:%d", __LINE__, i);
            }

            // set buffer sequence
            bcbuf = mBCBuffer + index;
            bcbuf->sequence = i;

            // get RGB565 thumbnail data from NV12 postview
            mCamera->toRGB565(mPostViewWidth, mPostViewHeight, mPostViewFormat,
                                postview_out, bcbuf->pdst_thumbnail);

            // shutter sound
            if (mMsgEnabled & CAMERA_MSG_SHUTTER)
                mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
            LOG1("BC, line:%d, shutter:%d", __LINE__, i);

            // do nothing for RAW message
            if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
                LOG1("BC, line:%d,do nothing for CAMERA_MSG_RAW_IMAGE", __LINE__);
            }

            if (!memory_userptr) {
                memcpy(bcbuf->psrc, main_out, bcbuf->src_size);
            }

            // mark the src data ready
            bcbuf->ready = true;
            LOG1("BC, line:%d, index:%d, ready:%d, sequence:%d", __LINE__, index, bcbuf->ready, bcbuf->sequence);

            // active the compress thread
            if (i == 0) {
                LOG1("BC, line:%d, send the signal to compressthread", __LINE__);
                mCompressCondition.signal();
            }

            // let the compress thread to encode the jpeg
            LOG1("BC, line:%d, before sem_post:sem_bc_captured, %d", __LINE__, i);
            if ((ret = sem_post(&sem_bc_captured)) < 0)
                LOGE("BC, line:%d, sem_post fail, ret:%d", __LINE__, ret);
            LOG1("BC, line:%d, after sem_post:sem_bc_captured, %d", __LINE__, i);

            //Postview
            if (use_texture_streaming) {
                int mPostviewId = 0;
                memcpy(mRawIdHeap->base(), &mPostviewId, sizeof(int));
                mDataCb(CAMERA_MSG_POSTVIEW_FRAME, mRawIdBase, mCallbackCookie);
                LOGD("Sent postview frame id: %d", mPostviewId);
            } else {
                /* TODO: YUV420->RGB565 */
                sp<MemoryBase> pv_buffer = new MemoryBase(mRawHeap, 0, mPostViewSize);
                mDataCb(CAMERA_MSG_POSTVIEW_FRAME, pv_buffer, mCallbackCookie);
                pv_buffer.clear();
            }

            // q buf, don't need to do it.
            //mCamera->putSnapshot(index);
        }
        LOG1("BC, line:%d, finished capture", __LINE__);
    }

    // find the and wait desired buffer
    bcbuf = mBCBuffer;
    for (i = 0; i < mBCNumReq; i++) {
        bcbuf = mBCBuffer + i;
        if ((bcbuf->sequence + 1) == mBCNumCur) {
            if ((ret = sem_wait(&sem_bc_encoded)) < 0)
                LOGE("BC, line:%d, sem_wait fail, ret:%d", __LINE__, ret);
            LOG1("BC, line:%d, sem_wait sem_bc_encoded, i:%d", __LINE__, i);
            break;
        }
    }
    if (i == mBCNumReq) {
        LOGE("BC, line:%d, error, i:%d == mBCNumReq", __LINE__, i);
        goto BCHANDLE_ERR;
    }

    if (mBCNumCur == mBCNumReq) {
        LOG1("BC, line:%d, begin to stop the camera", __LINE__);
        //clean up bcd
        mCamera->releasePostviewBcd();
        //Stop the Camera Now
        mCamera->stopSnapshot();
        //Set captureInProgress earlier.
        mCaptureInProgress = false;

        mAAA->IspSetFd(-1);
    }

    // send compressed jpeg image to upper
    JpegBuffer = new MemoryBase(bcbuf->heap, bcbuf->src_size, bcbuf->jpeg_size);
    mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, JpegBuffer, mCallbackCookie);
    JpegBuffer.clear();
    LOGD("BC, line:%d, send the %d, compressed jpeg image", __LINE__, i);

    mCaptureInProgress = false;

    if (mBCNumCur == mBCNumReq) {
        LOG1("BC, line:%d, begin to clean up the memory", __LINE__);
        // release the memory
        for (i = 0; i < mBCNumReq; i++) {
            bcbuf = mBCBuffer + i;
            bcbuf->heap.clear();
        }
        mBCHeap.clear();

        burstCaptureInit();
    }

    LOG1("BC, %s :end", __func__);
    return NO_ERROR;

BCHANDLE_ERR:
    LOGE("BC, line:%d, got BCHANDLE_ERR in the burstCaptureHandle", __LINE__);
    if (mBCBuffer) {
        for (i = 0; i < mBCNumReq; i++) {
            bcbuf = mBCBuffer + i;
            bcbuf->heap.clear();
        }
    }
    if (mBCHeap != NULL)
        mBCHeap.clear();

    mCamera->stopSnapshot();

    mCaptureInProgress = false;
    return UNKNOWN_ERROR;
}

#define MAX_FRAME_WAIT 3
#define FLASH_FRAME_WAIT 4
int CameraHardware::pictureThread()
{
    LOGD("%s :start", __func__);

    // ToDo. abstract some functions for both single capture and burst capture.
    if (mBCEn) {
        mCamera->setSnapshotNum(mBCNumReq);
        mBCNumCur++;
        LOGD("BC, line:%d, BCEn:%d, BCReq:%d, BCCur:%d", __LINE__, mBCEn, mBCNumReq, mBCNumCur);
        if (mBCNumCur == 1) {
            if (mCompressThread->run("CameraCompressThread", PRIORITY_DEFAULT) !=
            NO_ERROR) {
                LOGE("%s : couldn't run compress thread", __func__);
                return INVALID_OPERATION;
            }
        }
        return burstCaptureHandle();
    } else {
        mCamera->setSnapshotNum(1);
    }

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
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
    HWLibjpegWrap libjpghw;
    void* usrptr=NULL;
    bool bHwEncodepath=TRUE;
    //Although enable hwlibjpeg buffer share, if picture resolution is below 640*480, we have to go software path
    if(V4L2_PIX_FMT_YUV420 == mPicturePixelFormat)
        bHwEncodepath=FALSE;
#endif
    mCamera->getPostViewSize(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    mCamera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);
    rgb_frame_size = cap_width * cap_height * 2;

    //For postview
    sp<MemoryBase> pv_buffer = new MemoryBase(mRawHeap, 0, mPostViewSize);

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
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

#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
        if(bHwEncodepath){
            //initialize buffer share with hardware libjpeg
            if(libjpghw.initHwBufferShare((JSAMPLE *)pmainimage,capsize_aligned,cap_width,cap_height,(void**)&usrptr) != 0){
                LOGD("%s- initHwBufferShare Fail!",__func__);
                goto start_error_out;
            }
        }
        else
            usrptr =  pmainimage ;//software path, we have to set usrptr to memory allocated in camera hal
#endif

        if (memory_userptr) {
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
            mCamera->setSnapshotUserptr(0,usrptr, mRawHeap->getBase());
#else
            mCamera->setSnapshotUserptr(0, pmainimage, mRawHeap->getBase());
#endif
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&pic_thread_start,  0);
#endif
        //Prepare for the snapshot
        int fd;
        if ((fd = mCamera->startSnapshot()) < 0)
            goto start_error_out;

        //Flush 3A results
        mAAA->FlushManualSettings ();
        update3Aresults();
#ifdef PERFORMANCE_TUNING
        gettimeofday(&snapshot_start, 0);
#endif
        if(!mFlashNecessary)
            mCamera->setIndicatorIntensity(INDICATOR_INTENSITY_WORKING);

        //Skip the first frame
        //Don't need the flash for the skip frame
        index = mCamera->getSnapshot(&main_out, &postview_out, NULL);
        if (index < 0) {
            picHeap.clear();
            goto get_img_error;
        }

        //Turn on flash if neccessary before the Qbuf
        if (mFlashNecessary)
            mCamera->captureFlashOnCertainDuration(0, 800, 15*625); /* software trigger, 800ms, intensity 15*/
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

        if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
            ssize_t offset = exif_offset + thumbnail_offset;
            sp<MemoryBase> mBuffer = new MemoryBase(picHeap, offset, cap_frame_size);
            mDataCb(CAMERA_MSG_RAW_IMAGE, mBuffer, mCallbackCookie);
            mBuffer.clear();
        }

        if (!memory_userptr) {
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
            memcpy(usrptr, main_out, cap_frame_size);
#else
            memcpy(pmainimage, main_out, rgb_frame_size);
#endif
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&second_frame, 0);
#endif

        //Postview
        if (use_texture_streaming) {
            int mPostviewId = 0;
            memcpy(mRawIdHeap->base(), &mPostviewId, sizeof(int));
            mDataCb(CAMERA_MSG_POSTVIEW_FRAME, mRawIdBase, mCallbackCookie);
            LOGD("Sent postview frame id: %d", mPostviewId);
        } else {
            /* TODO: YUV420->RGB565 */
            mDataCb(CAMERA_MSG_POSTVIEW_FRAME, pv_buffer, mCallbackCookie);
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&postview, 0);
#endif
        mCamera->setIndicatorIntensity(INDICATOR_INTENSITY_OFF);

        mCamera->acheiveEXIFAttributesFromDriver();

        //Stop the Camera Now
        mCamera->putSnapshot(index);
        mCamera->releasePostviewBcd();
        mCamera->stopSnapshot();
        mAAA->IspSetFd(-1);

        //De-initialize file input
        if (use_file_input)
            mCamera->deInitFileInput();

        SnapshotPostProcessing (main_out, cap_width, cap_height);

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

#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
            if(bHwEncodepath){
                //set parameter for jpeg encode
                libjpghw.setJpeginfo(cap_width,cap_height,3,JCS_YCbCr,main_quality);
                if(libjpghw.startJPEGEncodebyHwBufferShare() != 0){
                      LOGD("%s- jpeg_destroy_compress done !",__func__);
                      goto get_img_error;
                }
                if(libjpghw.getJpegSize() > 0){
                     //there should jpeg data in pmainimage now
                     LOGD("%s- jpeg compress size = %d !",__func__,libjpghw.getJpegSize());
                     mainimage_size = libjpghw.getJpegSize();
                }
                else{
                    LOGD("%s- jpeg compress fail !",__func__);
                    goto get_img_error;
                }
            }
            else{
                 //software path
                 // RGB565 color space conversion
                 mCamera->toRGB565(cap_width, cap_height, mPicturePixelFormat, pmainimage, pmainimage);
                 // encode the main image
                 if (encodeToJpeg(cap_width, cap_height, pmainimage, pmainimage, &mainimage_size, main_quality) < 0)
                     goto start_error_out;
            }
#else
             // RGB565 color space conversion
             mCamera->toRGB565(cap_width, cap_height, mPicturePixelFormat, pmainimage, pmainimage);

             // encode the main image
             if (encodeToJpeg(cap_width, cap_height, pmainimage, pmainimage, &mainimage_size, main_quality) < 0)
                 goto start_error_out;
#endif

            // encode the thumbnail
            void *pdst = pthumbnail;
            if (encodeToJpeg(mPostViewWidth, mPostViewHeight, pthumbnail, pdst, &thumbnail_size, thumbnail_quality) < 0)
                goto start_error_out;

            thumbnail_size = thumbnail_size -  sizeof(FILE_END);
            memcpy(pdst, FILE_START, sizeof(FILE_START));

            // fill the attribute
            if ((unsigned int)thumbnail_size >= exif_offset) {
                // thumbnail is in the exif, so the size of it must less than exif_offset
                exifAttribute(exifattribute, cap_width, cap_height, false, mFlashNecessary);
            } else {
                exifAttribute(exifattribute, cap_width, cap_height, true, mFlashNecessary);
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
            void *psrc = (void*)((char*)pmainimage+sizeof(FILE_START));
            memcpy(pjpg_start, FILE_START, sizeof(FILE_START));
            memcpy(pjpg_exifend, FILE_END, sizeof(FILE_END));
            memmove(pjpg_main, psrc, mainimage_size-sizeof(FILE_START));
            jpeg_file_size =sizeof(FILE_START) + exif_size + sizeof(FILE_END) + mainimage_size- sizeof(FILE_END);

            LOG1("jpg file sz:%d", jpeg_file_size);

            sp<MemoryBase> JpegBuffer = new MemoryBase(picHeap, 0, jpeg_file_size);
            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, JpegBuffer, mCallbackCookie);
            JpegBuffer.clear();
        }
#ifdef PERFORMANCE_TUNING
        gettimeofday(&jpeg_encoded, 0);
#endif

        //clean up
        pv_buffer.clear();
        picHeap.clear();
    }
out:
    pv_buffer.clear();
    mCaptureInProgress = false;
    LOG1("%s :end", __func__);

    return NO_ERROR;

get_img_error:
    LOGE("Get the snapshot error, now stoping the camera\n");
    mCamera->stopSnapshot();

    if (use_file_input)
        mCamera->deInitFileInput();

start_error_out:
    mCaptureInProgress = false;
    mNotifyCb(CAMERA_MSG_ERROR, CAMERA_ERROR_UKNOWN, 0, mCallbackCookie);
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
    runPreFlashSequence();
    stopPreview();
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

    if (mSensorType == SENSOR_TYPE_SOC) {
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, 1, 0, mCallbackCookie);
        return NO_ERROR;
    }

    //stop the preview 3A thread
    mAeAfAwbLock.lock();
    if (mPreviewAeAfAwbRunning) {
        mPreviewAeAfAwbRunning = false;
        LOG1("%s : waiting for 3A thread to exit", __func__);
        mAeAfAwbEndCondition.wait(mAeAfAwbLock);
    }
    mAeAfAwbLock.unlock();

    if (mExitAutoFocusThread) {
        LOG1("%s : exiting on request", __func__);
        return NO_ERROR;
    }

    LOG1("%s: begin do the autofocus\n", __func__);
    //set the mFlashNecessary
    calculateLightLevel();
    switch(mCamera->getFlashMode())
    {
        case CAM_AE_FLASH_MODE_AUTO:
            if(!mFlashNecessary) break;
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
        af_status = runStillAfSequence();
    }
    else
    {
        //manual focus, just return focused
        af_status = true;
    }

    mCamera->setAssistIntensity(ASSIST_INTENSITY_OFF);
    if (af_status == FOCUS_CANCELD)
        return NO_ERROR;

    if(CAM_AF_MODE_TOUCH == af_mode) {
        mAAA->SetAwbEnabled(true);
        mAAA->SetAeEnabled(true);
        mPreviewAeAfAwbRunning = true;
        mPreviewAeAfAwbCondition.signal();
    }


    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, af_status , 0, mCallbackCookie);
    LOG1("%s : exiting with no error", __func__);
    return NO_ERROR;
}

int CameraHardware::runStillAfSequence(void)
{
    //The preview thread is stopped at this point
    bool af_status = false;
    mAAA->AeLock(true);
    mAAA->SetAeEnabled(false);
    mAAA->SetAfEnabled(true);
    mAAA->SetAwbEnabled(false);
    mAAA->AfStillStart();
    // Do more than 100 time
    for (int i = 0; i < mStillAfMaxCount; i++) {
        mAeAfAwbLock.lock();
        //check whether exit before wait.
        if (mExitAutoFocusThread) {
            LOGD("%s : exiting on request", __func__);
            mAeAfAwbLock.unlock();
            return FOCUS_CANCELD;//cancel
        }

        mPreviewFrameCondition.wait(mAeAfAwbLock);
        LOG2("%s: still AF return from wait", __func__);
        mAeAfAwbLock.unlock();
        mAAA->AeAfAwbProcess(true);
        mAAA->AfStillIsComplete(&af_status);
        if (af_status)
        {
            LOGD("==== still AF converge frame number %d\n", i);
            break;
        }
    }
    LOGD("==== still Af status (1: success; 0: failed) = %d\n", af_status);

    mAAA->AfStillStop ();
    mAAA->AeLock(false);
    mAAA->SetAfEnabled(false);

    return af_status;
}

status_t CameraHardware::sendCommand(int32_t command, int32_t arg1,
                                     int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardware::release()
{
    LOGD("%s start:", __func__);

    if (mAeAfAwbThread != NULL) {
        mAeAfAwbThread->requestExit();
        mPreviewAeAfAwbRunning = true;
        mPreviewAeAfAwbCondition.signal();
        mExitAeAfAwbThread = true;
        mPreviewFrameCondition.signal();
        LOG1("%s waiting 3A thread to exit:", __func__);
        mAeAfAwbThread->requestExitAndWait();
        mAeAfAwbThread.clear();
    }

    LOG1("%s deleted the 3A thread:", __func__);
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
        mAutoFocusThread->requestExit();
        mExitAutoFocusThread = true;
        mAeAfAwbEndCondition.signal();
        mPreviewFrameCondition.signal();
        mAutoFocusThread->requestExitAndWait();
        mAutoFocusThread.clear();
    }
    LOG1("%s deleted the autofocus thread:", __func__);

    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
    }
    LOG1("%s deleted the picture thread:", __func__);

    if (mCompressThread != NULL) {
        mCompressThread->requestExitAndWait();
        mCompressThread.clear();
    }
    LOG1("BC, line:%d, deleted the compress thread:", __LINE__);
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
            new_value = p.get(CameraParameters::KEY_FOCUS_MODE);
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
            else if(!strcmp(new_value, CameraParameters::FOCUS_MODE_TOUCH))
                afmode = CAM_AF_MODE_TOUCH;
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
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
        mPicturePixelFormat = V4L2_PIX_FMT_NV12;
#else
        mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
#endif
    else {
        LOGE("Only jpeg still pictures are supported, new_format:%s", new_format);
    }

    LOGD(" - Picture pixel format = new \"%s\"", new_format);
    p.getPictureSize(&new_picture_width, &new_picture_height);

    // burst capture
    mBCNumReq = p.getInt(CameraParameters::KEY_BURST_LENGTH);
    mBCEn = (mBCNumReq > 1) ? true : false;
    if (mBCEn) {
        mBCNumSkipReq = p.getInt(CameraParameters::KEY_BURST_SKIP_FRAMES);
        mPicturePixelFormat = V4L2_PIX_FMT_NV12;
        // ToDo. we will use the hw jpeg encoder and change the format to YUV420.
    } else {
        mBCNumReq = 1;
        mBCNumSkipReq = 0;
        mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
    }
    LOG1("BC, line:%d,burst len, en:%d, reqnum:%d, skipnum:%d", __LINE__, mBCEn, mBCNumReq, mBCNumSkipReq);
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
        /*there is limitation for picture resolution with hwlibjpeg buffer share
        if picture resolution below 640*480 , we have to set mPicturePixelFormat
        back to YUV420 and go through software encode path*/
        if(new_picture_width <= 640 || new_picture_height <=480)
            mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
        else
            mPicturePixelFormat = V4L2_PIX_FMT_NV12;
#endif
    LOGD("%s : new_picture_width %d new_picture_height = %d", __func__,
         new_picture_width, new_picture_height);

    // ToDo. removed it next patch.
    if (mBCEn)
        mPicturePixelFormat = V4L2_PIX_FMT_NV12;
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
    int vfmode = p.getInt("camera-mode");
    int mVideoFormat = (mSensorType == SENSOR_TYPE_SOC) ?
        V4L2_PIX_FMT_YUV420 : V4L2_PIX_FMT_NV12;
    //Deternmine the current viewfinder MODE.
    if (vfmode == 1) {
        LOG1("%s: Entering the video recorder mode\n", __func__);
        Mutex::Autolock lock(mRecordLock);
        mVideoPreviewEnabled = true; //viewfinder running in video mode
    } else {
        LOG1("%s: Entering the normal preview mode\n", __func__);
        Mutex::Autolock lock(mRecordLock);
        mVideoPreviewEnabled = false; //viewfinder running in preview mode
    }

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
    if (mSensorType != SENSOR_TYPE_SOC)
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
    int zoom = new_params.getInt(CameraParameters::KEY_ZOOM);
    mCamera->set_zoom_val(zoom);
    // Color Effect
	int effect = old_params.getInt(CameraParameters::KEY_EFFECT);
	new_value = new_params.get(CameraParameters::KEY_EFFECT);
	set_value = old_params.get(CameraParameters::KEY_EFFECT);
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
            LOGD("Changed effect to %s", new_params.get(CameraParameters::KEY_EFFECT));
        }
    }

    // xnr
    int xnr = old_params.getInt(CameraParameters::KEY_XNR);
    new_value = new_params.get(CameraParameters::KEY_XNR);
    set_value = old_params.get(CameraParameters::KEY_XNR);
    LOGD(" - xnr = new \"%s\" (%d) / current \"%s\"",new_value, xnr, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp(new_value, "false"))
            ret = mCamera->setXNR(false);
        else if (!strcmp(new_value, "true"))
            ret = mCamera->setXNR(true);
        if (!ret) {
            LOGD("Changed xnr to %s", new_params.get(CameraParameters::KEY_XNR));
        }
    }
    // gdc/cac
    int gdc = old_params.getInt(CameraParameters::KEY_GDC);
    new_value = new_params.get(CameraParameters::KEY_GDC);
    set_value = old_params.get(CameraParameters::KEY_GDC);
    LOGD(" - gdc = new \"%s\" (%d) / current \"%s\"",new_value, gdc, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp(new_value, "false"))
            ret = mCamera->setGDC(false);
        else if (!strcmp(new_value, "true"))
            ret = mCamera->setGDC(true);
        if (!ret) {
            LOGD("Changed gdc to %s", new_params.get(CameraParameters::KEY_GDC));
        }
    }

    // DVS
    int dvs = old_params.getInt(CameraParameters::KEY_DVS);
    new_value = new_params.get(CameraParameters::KEY_DVS);
    set_value = old_params.get(CameraParameters::KEY_DVS);
    LOGD(" - dvs = new \"%s\" (%d) / current \"%s\"",new_value, dvs, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp(new_value, "false")) {
            ret = mCamera->setDVS(false);
            mDVSProcessing = false;
        }
        else if (!strcmp(new_value, "true")) {
            ret = mCamera->setDVS(true);
            mDVSProcessing = true;
        }
        if (!ret) {
            LOGD("Changed dvs to %s", new_params.get(CameraParameters::KEY_DVS));
        }
    }

    // tnr
    int tnr = old_params.getInt(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION);
    new_value = new_params.get(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION);
    set_value = old_params.get(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION);
    LOGD(" - temporal-noise-reduction = new \"%s\" (%d) / current \"%s\"",new_value, tnr, set_value);
    if (strcmp(set_value, new_value) != 0) {
        if (!strcmp(new_value, "on"))
            ret = mCamera->setTNR(true);
        else if (!strcmp(new_value, "off"))
            ret = mCamera->setTNR(false);
        if (!ret) {
            LOGD("Changed temporal-noise-reduction to %s",
                    new_params.get(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION));
        }
    }

#ifdef TUNING_EDGE_ENHACNMENT
    // nr and ee
    int nr_ee = old_params.getInt(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
    new_value = new_params.get(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
    set_value = old_params.get(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
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
                    new_params.get(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT));
        }
    }
#endif

    //macc
    int color = 0;
    int macc = old_params.getInt(CameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
    new_value = new_params.get(CameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
    set_value = old_params.get(CameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
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

int CameraHardware::calculateLightLevel()
{
    if (mBCEn) {
        mFlashNecessary = false;
        return 0;
    } else
        return mAAA->AeIsFlashNecessary (&mFlashNecessary);
}

void CameraHardware::runPreFlashSequence(void)
{
    int index;
    void *data;

    if (!mFlashNecessary)
        return ;
    mAAA->SetAeEnabled (true);
    mAAA->SetAwbEnabled (true);

    // pre-flash process
    index = mCamera->getPreview(&data);
    if (index < 0) {
        LOGE("%s: Error to get frame\n", __func__);
        return ;
    }
    mAAA->PreFlashProcess(CAM_FLASH_STAGE_NONE);

    // pre-flash
//    captureFlashOff();
    mCamera->putPreview(index);
    index = mCamera->getPreview(&data);
    if (index < 0) {
        LOGE("%s: Error to get frame\n", __func__);
        return ;
    }

    mAAA->PreFlashProcess(CAM_FLASH_STAGE_PRE);

    // main flash
    mCamera->captureFlashOnCertainDuration(0, 100, 1*625);  /* software trigger, 100ms, intensity 1*/
    mCamera->putPreview(index);
    index = mCamera->getPreview(&data);
    if (index < 0) {
        LOGE("%s: Error to get frame\n", __func__);
        return ;
    }

    mAAA->PreFlashProcess(CAM_FLASH_STAGE_MAIN);

    mAAA->SetAeEnabled (false);
    mAAA->SetAwbEnabled (false);
    mCamera->putPreview(index);
}

//3A processing
void CameraHardware::update3Aresults(void)
{
    LOG1("%s\n", __func__);
    mAAA->AeLock(true);
    mAAA->SetAeEnabled (true);
    mAAA->SetAfEnabled(true);
    mAAA->SetAwbEnabled(true);
    mAAA->AeAfAwbProcess (false);
    int af_mode;
    mAAA->AfGetMode (&af_mode);
    if (af_mode != CAM_AF_MODE_MANUAL)
        mAAA->AfApplyResults();
    mAAA->AeLock(false);
    mAAA->SetAeEnabled (false);
    mAAA->SetAfEnabled(false);
    mAAA->SetAwbEnabled(false);
}

int CameraHardware::SnapshotPostProcessing(void *img_data, int width, int height)
{
    // do red eye removal
    int img_size;

    // FIXME:
    // currently, if capture resolution more than 5M, camera will hang if
    // ShRedEye_Remove() is called in 3A library
    // to workaround and make system not crash, maximum resolution for red eye
    // removal is restricted to be 5M
    if (width > 2560 || height > 1920)
    {
        LOGD(" Bug here: picture size must not more than 5M for red eye removal\n");
        return -1;
    }

    img_size = mCamera->m_frameSize (mPicturePixelFormat, width, height);

    mAAA->DoRedeyeRemoval (img_data, img_size, width, height, mPicturePixelFormat);

    return 0;
}

int CameraHardware::checkSensorType(int cameraId)
{
    int type;
    if (num_camera == 1)
        type = camera_info[0].type;
    else {
        if (cameraId == 0)
            type = camera_info[primary_camera_id].type;
        else
            type = camera_info[secondary_camera_id].type;
    }
    return type;
}

void CameraHardware::setupPlatformType(void)
{
    int i, j;
    for (i = 0; i < MAX_CAMERAS; i++) {
        //Remove the blank and i2c name
        for (j = 0; j < MAX_SENSOR_NAME_LENGTH; j ++) {
            if (camera_info[i].name[j] == ' ') {
                camera_info[i].name[j] = '\0';
                break;
            }
        }
        LOGE("%s: sensor name is %s", __func__, camera_info[i].name);

        if (!strcmp(camera_info[i].name, CDK_PRIMARY_SENSOR_NAME)) {
            camera_info[i].platform = MFLD_CDK_PLATFORM;
            camera_info[i].type = ci_adv_sensor_dis_14m;
        } else if (!strcmp(camera_info[i].name, CDK_SECOND_SENSOR_NAME)) {
            camera_info[i].platform = MFLD_CDK_PLATFORM;
            camera_info[i].type = ci_adv_sensor_ov2720_2m;
        } else if (!strcmp(camera_info[i].name, PR2_PRIMARY_SENSOR_NAME)) {
            camera_info[i].platform = MFLD_PR2_PLATFORM;
            camera_info[i].type = ci_adv_sensor_liteon_8m;
        } else if (!strcmp(camera_info[i].name, PR2_SECOND_SENSOR_NAME)) {
            camera_info[i].platform = MFLD_PR2_PLATFORM;
            camera_info[i].type = ci_adv_sensor_soc;
        } else
            LOGE("%s: Unknow platform", __func__);
    }
}


//----------------------------------------------------------------------------
//----------------------------HAL--used for camera service--------------------
static int HAL_cameraType[MAX_CAMERAS];
static CameraInfo HAL_cameraInfo[MAX_CAMERAS] = {
    {
        CAMERA_FACING_BACK,
#ifdef MFLD_CDK
        270,  /* default orientation, we will modify it at other place, ToDo */
#else
        90,
#endif
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
    int ret;
    struct v4l2_input input;
    int fd = -1;
    char *dev_name = "/dev/video0";

    fd = open(dev_name, O_RDWR);
    if (fd <= 0) {
        LOGE("ERR(%s): Error opening video device %s: %s", __func__,
             dev_name, strerror(errno));
        return 0;
    }

    int i;
    for (i = 0; i < MAX_CAMERAS; i++) {
        memset(&input, 0, sizeof(input));
        input.index = i;
        ret = ioctl(fd, VIDIOC_ENUMINPUT, &input);
        if (ret < 0) {
            break;
        }
        camera_info[i].type = input.reserved[0];
        camera_info[i].port = input.reserved[1];
        strncpy(camera_info[i].name, (const char *)input.name, MAX_SENSOR_NAME_LENGTH);

        if (camera_info[i].type != SENSOR_TYPE_RAW &&
            camera_info[i].type != SENSOR_TYPE_SOC) {
            break;
        }
    }

    close(fd);

    num_camera = i;

    for (i = 0; i < num_camera; i++) {
        if (camera_info[i].port == PRIMARY_MIPI_PORT) {
            primary_camera_id = i;
        } else if (camera_info[i].port == SECONDARY_MIPI_PORT) {
            secondary_camera_id = i;
        }
    }

    return num_camera;
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
