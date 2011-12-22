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
#include "LogHelper.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <CameraParameters.h>
#include "SkBitmap.h"
#include "SkImageEncoder.h"
#include "SkStream.h"

#include <string.h>

#if ENABLE_BUFFER_SHARE_MODE
#include <IntelBufferSharing.h>
#endif
#include <ui/android_native_buffer.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferMapper.h>

#define FLASH_FRAME_TIMEOUT   5

namespace android {

static cameraInfo camInfo[MAX_CAMERAS];
int CameraHardware::num_cameras = 0;

static int HAL_cameraType[MAX_CAMERAS];
static camera_info HAL_cameraInfo[MAX_CAMERAS] = {
    {
        CAMERA_FACING_FRONT,
        180,
    },
    {
        CAMERA_FACING_BACK,
        0,
    }
};

static inline long calc_timediff(struct timeval *t0, struct timeval *t1)
{
    return ((t1->tv_sec - t0->tv_sec) * 1000000 + t1->tv_usec - t0->tv_usec) / 1000;
}

CameraHardware::CameraHardware(int cameraId)
    :
    mPreviewWindow(0),
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
    mDataCbTimestamp(0),
    awb_to_manual(false),
    mCanFlip(false)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int i, ret, camera_idx = -1;

    mPreviewBuffer.mem = NULL;
    mRecordingBuffer.mem = NULL;
    // Hardcoded to front camera untill the back camera driver is fixed!
    //mCameraId = 1;
    LogDetail("Create the CameraHardware for %s camera", mCameraId == CAMERA_FACING_BACK ? "back" : "front");
    mCamera = IntelCamera::createInstance();

    if (mCamera == NULL) {
        LogError("Fail on mCamera object creation");
    }else
    {
        mCamera->deinitCamera();
    }

    setupPlatformType();
    /* The back facing camera is assumed to be the high resolution camera which
     * uses the primary MIPI CSI2 port. */
    for (i = 0; i < getNumberOfCameras(); i++) {
        if ((mCameraId == CAMERA_FACING_BACK  && camInfo[i].port == ATOMISP_CAMERA_PORT_PRIMARY) ||
	    (mCameraId == CAMERA_FACING_FRONT && camInfo[i].port == ATOMISP_CAMERA_PORT_SECONDARY)) {
		camera_idx = i;
		break;
        }
    }
    if (camera_idx == -1) {
	    LogError(" Did not find %s camera\n",
		 mCameraId == CAMERA_FACING_BACK ? "back" : "front");
	    camera_idx = 0;
    }

    // Create the 3A object
    mAAA = new AAAProcess();

    // Create the ISP object
    ret = mCamera->initCamera(mCameraId, camera_idx, mSensorType, mAAA);
    if (ret < 0) {
        LogError("Failed to initialize camera");
    }
    // Init 3A for RAW sensor only
    mSensorType = mAAA->Init(camInfo[i].name, mCamera->getFd());
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
    mHwJpegBufferShareEn = true;
    mPicturePixelFormat = V4L2_PIX_FMT_NV12;
    if (memory_userptr == 0) {
        LogError("jpeg buffer share set but user pointer unset");
    }
#else
    mHwJpegBufferShareEn = false;
    mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
#endif

    initDefaultParameters();
    mVideoPreviewEnabled = false;
    mFlashNecessary = false;

    mExitAutoFocusThread = true;
    mExitPreviewThread = false;
    mExitAeAfAwbThread = false;
    mPreviewRunning = false;
    mPreviewAeAfAwbRunning = false;
    mRecordRunning = false;
    mPreviewThread = new PreviewThread(this);
    mAutoFocusThread = new AutoFocusThread(this);
    mPictureThread = new PictureThread(this);

    mCompressThread = new CompressThread(this);
    mDvsThread = new DvsThread(this);
    mExitDvsThread = false;
    mManualFocusPosi = 0;

    if (mSensorType == SENSOR_TYPE_RAW) {
        mAeAfAwbThread = new AeAfAwbThread(this);
        mAAA->SetAfEnabled(true);
        mAAA->SetAeEnabled(true);
        mAAA->SetAwbEnabled(true);
    } else {
        mAeAfAwbThread = NULL;
    }

    // the table is defined in CameraHardware.h
    // the values should be defined by the application
    // at the moment they are hard-coded here
    WeightTable[0] = 1;
    WeightTable[1] = 2;
    WeightTable[2] = 1;
    WeightTable[3] = 2;
    WeightTable[4] = 3;
    WeightTable[5] = 2;
    WeightTable[6] = 1;
    WeightTable[7] = 2;
    WeightTable[8] = 1;
    mAeWeightMap.num_windows_x = 3;
    mAeWeightMap.num_windows_y = 3;
    mAeWeightMap.weights = WeightTable;

    // burst capture initialization
    if ((ret = sem_init(&sem_bc_captured, 0, 0)) < 0)
        LogError("BC, line:%d, sem_init fail, ret:%d", __LINE__, ret);
    if ((ret = sem_init(&sem_bc_encoded, 0, 0)) < 0)
        LogError("BC, line:%d, sem_init fail, ret:%d", __LINE__, ret);
    burstCaptureInit(true);


#if ENABLE_BUFFER_SHARE_MODE
    isVideoStarted = false;
    isCameraTurnOffBufferSharingMode = false;
#endif
    LogDetail("libcamera version: 2011-08-03 1.0.1");
    LogDetail("Using sensor %s (%s)",
         camInfo[camera_idx].name,
	 mSensorType == SENSOR_TYPE_RAW ? "RAW" : "SOC");
#ifdef MFLD_CDK
    LogDetail("initialize on CDK platform");
#else
    LogDetail("initialize on PR2 platform");
#endif
}

CameraHardware::~CameraHardware()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;

    if (mPreviewBuffer.mem != NULL) {
        mPreviewBuffer.mem->release(mPreviewBuffer.mem);
    }
    if (mRecordingBuffer.mem != NULL) {
        mRecordingBuffer.mem->release(mRecordingBuffer.mem);
    }

    if (mRawMem != NULL) {
        mRawMem->release(mRawMem);
    }

    if ((ret = sem_destroy(&sem_bc_captured)) < 0)
        LogError("BC, line:%d, sem_destroy fail, ret:%d", __LINE__, ret);
    if ((ret = sem_destroy(&sem_bc_encoded)) < 0)
        LogError("BC, line:%d, sem_destroy fail, ret:%d", __LINE__, ret);

    if(mAAA!=NULL) mAAA->Uninit();
    delete mAAA;
    mAAA=NULL;
    if(mCamera!=NULL) mCamera->deinitCamera();
    mCamera = NULL;
    singleton = NULL;
}

void CameraHardware::initDefaultParameters()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    CameraParameters p;

    //common features for RAW and Soc
    p.setPreviewSize(640, 480);
    p.setPreviewFrameRate(30);
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

    char *resolution_dec = mCamera->getMaxSnapShotResolution();
    p.set("picture-size-values", resolution_dec);
    int ww,hh;
    mCamera->getMaxSnapshotSize(&ww,&hh);
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
    if(ww <= 640 || hh <=480)
        mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
    else{
        mPicturePixelFormat = V4L2_PIX_FMT_NV12;
    }
#endif
    mCamera->setSnapshotSize(ww,hh,mPicturePixelFormat);
    p.setPictureSize(ww,hh);

    //thumbnail size
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH,"320");
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,"240");
    p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES,"640x480,512x384,320x240,0x0");

    //focallength
    if(mCameraId == CAMERA_FACING_BACK)
        p.set(CameraParameters::KEY_FOCAL_LENGTH,"5.56");
    else
        p.set(CameraParameters::KEY_FOCAL_LENGTH,"2.78");

    //for CTS test ...
    // Vertical angle of view in degrees.
    p.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE,"42.5");
    p.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE,"54.8");

    // Supported number of preview frames per second.
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,"30,15,10");
    p.set(CameraParameters::KEY_PREVIEW_FPS_RANGE,"10500,30304");
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,"(10500,30304),(11000,30304),(11500,30304)");

    p.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT,CameraParameters::PIXEL_FORMAT_YUV420SP);

    //zoom
    p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");
    p.set(CameraParameters::KEY_MAX_ZOOM, "60");
    p.set(CameraParameters::KEY_ZOOM_RATIOS, "100,125,150,175,200,225,250,275,300,325,350,375,400,425,450,475,500,525,"
          "550,575,600,625,650,675,700,725,750,775,800,825,850,875,900,925,950,975,1000,1025,1050,1075,1100,"
          "1125,1150,1175,1200,1225,1250,1275,1300,1325,1350,1375,1400,1425,1450,1475,1500,1525,1550,1575,1600");
    p.set(CameraParameters::KEY_ZOOM, 0);

    if (mCameraId == CAMERA_FACING_BACK) {
        // For main back camera
        // flash mode option
        p.set(CameraParameters::KEY_FLASH_MODE,"off");
        p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"auto,off,on,torch,slow-sync,day-sync");
    } else {
        // For front camera
        // No flash present
        p.set(CameraParameters::KEY_FLASH_MODE,"off");
        p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES,"off");
    }

    //focus mode
    p.set(CameraParameters::KEY_FOCUS_MODE, "auto");
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, "auto");

    if (mSensorType == SENSOR_TYPE_RAW) {
        //ISP advanced features
        p.set(CameraParameters::KEY_EFFECT, "none");
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
        // back lighting correction
        p.set(CameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE,"off");
        p.set(CameraParameters::KEY_SUPPORTED_BACK_LIGHTING_CORRECTION_MODES,"on,off");
        // red eye removal
        p.set(CameraParameters::KEY_RED_EYE_MODE,"off");
        p.set(CameraParameters::KEY_SUPPORTED_RED_EYE_MODES,"on,off");

        //3A for RAW only
        // ae mode
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
        // awb mapping
        p.set(CameraParameters::KEY_AWB_MAPPING_MODE, "auto");
        p.set(CameraParameters::KEY_SUPPORTED_AWB_MAPPING_MODES, "auto,indoor,outdoor");
        // manual shutter control
        p.set(CameraParameters::KEY_SHUTTER, "60");
        p.set(CameraParameters::KEY_SUPPORTED_SHUTTER, "2s,1s,2,4,8,15,30,60,125,250,500");
        // manual iso control
        p.set(CameraParameters::KEY_ISO, "iso-200");
        p.set(CameraParameters::KEY_SUPPORTED_ISO, "iso-100,iso-200,iso-400,iso-800,iso-1600");
        // manual color temperature
        p.set(CameraParameters::KEY_COLOR_TEMPERATURE, "5000");
        // manual focus
        p.set(CameraParameters::KEY_FOCUS_DISTANCES, "2,2,Infinity");
        // RAW picture data format
        p.set(CameraParameters::KEY_RAW_DATA_FORMAT, "none");
        p.set(CameraParameters::KEY_SUPPORTED_RAW_DATA_FORMATS, "none,yuv,bayer");
        // focus window
        p.set("focus-window", "0,0,0,0");
    }

    mParameters = p;
    mFlush3A = true;
}

void CameraHardware::initPreviewBuffer(int size)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    unsigned int page_size = getpagesize();
    unsigned int size_aligned = (size + page_size - 1) & ~(page_size - 1);
    unsigned int postview_size = size_aligned;

/* If we are reinitialized with a different size, we must also reset the
 * buffers geometry.
 */
    if (mPreviewWindow != 0) {
        int preview_width, preview_height;
        mParameters.getPreviewSize(&preview_width, &preview_height);
        mPreviewWindow->set_buffers_geometry(
            mPreviewWindow,
            preview_width,
            preview_height,
            HAL_PIXEL_FORMAT_RGB_565);
    }

    if (size != mPreviewFrameSize) {
        if (mPreviewBuffer.mem != NULL)
            deInitPreviewBuffer();
        mPreviewBuffer.mem = mGetMemory(-1, size_aligned, PREVIEW_NUM_BUFFERS, NULL);
        LogDetail("mPreviewBuffer mem: %p (%dB)", mPreviewBuffer.mem->data, mPreviewBuffer.mem->size);
        mPreviewBuffer.baseSize = size_aligned;
        mRawMem = mGetMemory(-1, postview_size, 1, NULL);
        LogDetail("mRawMem mem: %p (%dB)", mRawMem->data, mRawMem->size);
        mRawIdMem = mGetMemory(-1, sizeof(int), 1, NULL);
        LogDetail("mRawIdMem mem: %p (%dB)", mRawIdMem->data, mRawIdMem->size);
        mPreviewConvertMem = mGetMemory(-1, size_aligned * 4 /3, 1, NULL);
        LogDetail("mPreviewConvertMem mem: %p (%dB)", mPreviewConvertMem->data, mPreviewConvertMem->size);

        for (int i = 0; i < PREVIEW_NUM_BUFFERS; i++) {
            mPreviewBuffer.flags[i] = 0;
            mPreviewBuffer.base[i] = (void*)((unsigned)mPreviewBuffer.mem->data + (i * size_aligned));
            mPreviewBuffer.start[i] = (uint8_t *)mPreviewBuffer.mem->data +
                                      (i * size_aligned);
            LogDetail2("mPreviewBuffer.start[%d] = %p", i, mPreviewBuffer.start[i]);
            clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        }
        LogDetail("PreviewBufferInfo: num(%d), size(%d), heapsize(%d)",
             PREVIEW_NUM_BUFFERS, size, mPreviewBuffer.mem->size);
        mPreviewFrameSize = size;
    }

    if (memory_userptr)
        for (int i = 0; i < PREVIEW_NUM_BUFFERS; i++)
            mCamera->setPreviewUserptr(i, mPreviewBuffer.start[i]);
}

void CameraHardware::deInitPreviewBuffer()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    for (int i=0; i < PREVIEW_NUM_BUFFERS; i++)
        mPreviewBuffer.base[i] = 0;
    if (mPreviewBuffer.mem != NULL)
        mPreviewBuffer.mem->release(mPreviewBuffer.mem);
    mPreviewBuffer.mem = NULL;
    if (mRawMem != NULL) {
        mRawMem->release(mRawMem);
    }
    mRawMem = NULL;
    if (mRawIdMem != NULL)
        mRawIdMem->release(mRawIdMem);
    mRawIdMem = NULL;
    if (mPreviewConvertMem != NULL)
        mPreviewConvertMem->release(mPreviewConvertMem);
    mPreviewConvertMem = NULL;
    mPreviewWindow = NULL;
}

status_t CameraHardware::setPreviewWindow(struct preview_stream_ops *window)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    mPreviewWindow = window;
    if (mPreviewWindow != 0) {
        int preview_width, preview_height;
        mParameters.getPreviewSize(&preview_width, &preview_height);
        LogDetail("Setting new preview window %p (%dx%d)", mPreviewWindow, preview_width, preview_height);
        mPreviewWindow->set_usage(mPreviewWindow, GRALLOC_USAGE_SW_WRITE_OFTEN);
        mPreviewWindow->set_buffers_geometry(
            mPreviewWindow,
            preview_width,
            preview_height,
            HAL_PIXEL_FORMAT_RGB_565);
    }
    return NO_ERROR;
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
    LogEntry(LOG_TAG, __FUNCTION__);
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
    if (mRecordingBuffer.mem != NULL)
        deInitRecordingBuffer();

    mRecordingBuffer.mem = mGetMemory(-1, size_aligned, PREVIEW_NUM_BUFFERS, NULL);
    mRecordingBuffer.baseSize = size_aligned;
    for (int i = 0; i < PREVIEW_NUM_BUFFERS; i++) {
        mRecordingBuffer.flags[i] = 0;
        mRecordingBuffer.base[i] = mRecordingBuffer.mem + (i * size_aligned);
        mRecordingBuffer.start[i] = (uint8_t *)mRecordingBuffer.mem
                                    + (i * size_aligned);
        mUserptrMem[i] = mGetMemory(-1, ptr_size, 1, NULL);
        clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        LogDetail("RecordingBufferInfo: num(%d), size(%d), heapsize(%d)",
                PREVIEW_NUM_BUFFERS, size, mRecordingBuffer.mem->size);

    }
    mRecorderFrameSize = size;
    mRecordConvertMem = mGetMemory(-1, size, 1, NULL);

    if (memory_userptr)
        for (int i = 0; i < PREVIEW_NUM_BUFFERS; i++)
            mCamera->setRecorderUserptr(i, mPreviewBuffer.start[i],
                                      mRecordingBuffer.start[i]);
}

void CameraHardware::deInitRecordingBuffer()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mRecordingBuffer.mem != NULL) {
        for (int i = 0; i < PREVIEW_NUM_BUFFERS; i++) {
            mRecordingBuffer.base[i] = 0;
            mUserptrMem[i]->release(mUserptrMem[i]);
        }
        mRecordingBuffer.mem->release(mRecordingBuffer.mem);
    }
    mRecordConvertMem->release(mRecordConvertMem);
}

void CameraHardware::setCallbacks(camera_notify_callback notify_cb,
                                  camera_data_callback data_cb,
                                  camera_data_timestamp_callback data_cb_timestamp,
                                  camera_request_memory get_memory,
                                  void* user)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mGetMemory = get_memory;
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
    LogEntry2(LOG_TAG, __FUNCTION__);
    //Copy the preview frame out.
    LogDetail2("Begin processPreviewFrame, buffer=%p\n", buffer);
    int previewFrame = mPreviewFrame;
    if (!isBFSet(mPreviewBuffer.flags[previewFrame], BF_ENABLED) &&
        !isBFSet(mPreviewBuffer.flags[previewFrame], BF_LOCKED)) {
        if (memory_userptr == 0) {
            setBF(&mPreviewBuffer.flags[previewFrame], BF_LOCKED);
            memcpy(mPreviewBuffer.start[previewFrame], buffer, mPreviewFrameSize);
            clrBF(&mPreviewBuffer.flags[previewFrame], BF_LOCKED);
        }
        setBF(&mPreviewBuffer.flags[previewFrame], BF_ENABLED);
    }
    mPreviewFrame = (previewFrame + 1) % PREVIEW_NUM_BUFFERS;
    // Notify the client of a new preview frame.
    int postPreviewFrame = mPostPreviewFrame;
    if (isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_ENABLED) &&
        !isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_LOCKED)) {
        /*
            ssize_t offset;
            size_t size;
            mPreviewBuffer.base[postPreviewFrame]->getMemory(&offset, &size);
            //If we delete the LOGV here, the preview is black
            LogDetail("%s: Postpreviwbuffer offset(%u), size(%u)\n", __FUNCTION__,
                 (int)offset, (int)size);
                 */
            if (mPreviewWindow != 0) {

                int preview_width, preview_height;
                mParameters.getPreviewSize(&preview_width, &preview_height);
                LogDetail2("copying raw image %d x %d  ", preview_width, preview_height);

                buffer_handle_t *buf;
                int err;
                int stride;
                if ((err = mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf, &stride)) != 0) {
                    LogError("Surface::dequeueBuffer returned error %d", err);
                } else {
                    if (mPreviewWindow->lock_buffer(mPreviewWindow, buf) != NO_ERROR) {
                        LogError("Failed to lock preview buffer!");
                        mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
                        return;
                    }
                    GraphicBufferMapper &mapper = GraphicBufferMapper::get();
                    const Rect bounds(preview_width, preview_height);
                    void *dst;
                    mCamera->toRGB565(preview_width,
                                      preview_height,V4L2_PIX_FMT_NV12,
                                      (unsigned char *) mPreviewBuffer.start[postPreviewFrame],
                                      (unsigned char *) mPreviewConvertMem->data);
                    if (mapper.lock(*buf, GRALLOC_USAGE_SW_WRITE_OFTEN, bounds, &dst) != NO_ERROR) {
                        LogError("Failed to lock GraphicBufferMapper!");
                        mPreviewWindow->cancel_buffer(mPreviewWindow, buf);
                        return;
                    }
                    memcpy(dst, mPreviewConvertMem->data, mPreviewFrameSize * 4 / 3);
                    if ((err = mPreviewWindow->enqueue_buffer(mPreviewWindow, buf)) != 0) {
                        LogError("Surface::queueBuffer returned error %d", err);
                    }
                    mapper.unlock(*buf);
                }
                buf = NULL;
        }
        clrBF(&mPreviewBuffer.flags[postPreviewFrame],BF_LOCKED|BF_ENABLED);
    }
    mPostPreviewFrame = (postPreviewFrame + 1) % PREVIEW_NUM_BUFFERS;
}

void CameraHardware::processRecordingFrame(void *buffer, int index)
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
        //Copy buffer out from driver
        int recordingFrame = index;

        if (!isBFSet(mRecordingBuffer.flags[recordingFrame], BF_ENABLED) &&
            !isBFSet(mRecordingBuffer.flags[recordingFrame], BF_LOCKED)) {
            setBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
#if ENABLE_BUFFER_SHARE_MODE
#else
            mRecordConvertMem->data = buffer;
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
            int offset = (int)(mRecordingBuffer.base[postRecordingFrame]) - (int)(mRecordingBuffer.mem->data);
            LogDetail("%s: Post Recording Buffer offset(%d), size(%d)\n", __FUNCTION__,
                offset, mRecordingBuffer.baseSize);

#if ENABLE_BUFFER_SHARE_MODE
            LogDetail2("Sending message: CAMERA_MSG_VIDEO_FRAME");
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME,
                            mUserptrMem[postRecordingFrame], 0, mCallbackCookie);
#else
            LogDetail2("Sending message: CAMERA_MSG_VIDEO)FRAME");
            mDataCbTimestamp(timestamp, CAMERA_MSG_VIDEO_FRAME,
                         mRecordConvertMem, 0, mCallbackCookie);
#endif
            LogDetail2("Sending the recording frame, size %d, index %d/%d\n",
                 mRecorderFrameSize, postRecordingFrame, PREVIEW_NUM_BUFFERS);
        }
    }
}

// ---------------------------------------------------------------------------
int CameraHardware::previewThread()
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    void *data;
    //DQBUF
    mPreviewLock.lock();
    //Checke whether the preview is running
    if (!mPreviewRunning) {
        mPreviewLock.unlock();
        return 0;
    }
    int index = mCamera->getPreview(&data, NULL);
    mPreviewLock.unlock();

    if (index < 0) {
        LogError("Fail on mCamera->getPreview()");
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
    if (!mExitPreviewThread && mPreviewRunning) {
        mPreviewLock.lock();
        mCamera->putPreview(index);
        mPreviewLock.unlock();
    }

    return NO_ERROR;
}

int CameraHardware::recordingThread()
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    void *main_out, *preview_out;
    bool bufferIsReady = true;
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
        LogError("Fail on mCamera->getRecording()");
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

    if (!mExitPreviewThread) {
        mPreviewLock.lock();
        mCamera->putRecording(index);
        mPreviewLock.unlock();
    }
    return NO_ERROR;
}

int CameraHardware::previewThreadWrapper()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int vf_mode;
    while (1) {
        mPreviewLock.lock();
        while (!mPreviewRunning) {
            LogInfo("preview is waiting");
            //do the stop here. Delay for the race condition with stopPreview
            mPreviewCondition.wait(mPreviewLock);
            LogInfo("preview return from wait");
        }
        mPreviewLock.unlock();

        if (mExitPreviewThread) {
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
                LogInfo("preview thread exit with error");
                return -1;
            }
        }
        // next frame loop
    }
}

int CameraHardware::aeAfAwbThread()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    while (1) {
        if (mExitAeAfAwbThread) {
            return 0;
        }

        mAeAfAwbLock.lock();
        while (!mPreviewAeAfAwbRunning) {
            LogInfo("previewaeafawb is waiting");
            //Tell stop preview to continue
            mAeAfAwbEndCondition.signal();
            mPreviewAeAfAwbCondition.wait(mAeAfAwbLock);
            LogInfo("previewaeafawb return from wait");
        }
        mAeAfAwbLock.unlock();
        //Check exit. Maybe we are waken up from the release. We don't go to
        //sleep again.
        if (mExitAeAfAwbThread) {
            return 0;
        }

        mAeAfAwbLock.lock();
        mPreviewFrameCondition.wait(mAeAfAwbLock);
        LogDetail2("3A return from wait");
        mAeAfAwbLock.unlock();
/* TODO: removed since is crashing libmfldadvci.so lib (need to debug later!!!)
        if (mAAA->AeAfAwbProcess(true) < 0) {
            LogWarning("3A return error");
            //mNotifyCb(CAMERA_MSG_ERROR, CAMERA_ERROR_UKNOWN, 0, mCallbackCookie);
        }
*/
        LogDetail2("After run 3A thread");

        if (mManualFocusPosi) {
            if (mAAA->AfSetManualFocus(mManualFocusPosi, true) == AAA_SUCCESS)
                mManualFocusPosi = 0;
        }
    }
}

void CameraHardware::initHeapLocked(int preview_size)
{
}

void CameraHardware::print_snapshot_time(void)
{
#ifdef PERFORMANCE_TUNING
    LOG1("stop preview: %ldms\n", calc_timediff(&picture_start, &preview_stop));
    LOG1("start picture thead %ldms\n", calc_timediff(&preview_stop, &pic_thread_start));
    LOG1("snapshot start %ldms\n", calc_timediff(&pic_thread_start, &snapshot_start));
    LOG1("take first frame %ldms\n", calc_timediff(&pic_thread_start, &first_frame));
    LOG1("take second frame %ldms\n", calc_timediff(&first_frame, &second_frame));
    LOG1("Postview %ldms\n", calc_timediff(&second_frame, &postview));
    LOG1("snapshot stop %ldms\n", calc_timediff(&postview, &snapshot_stop));
    LOG1("Jpeg encoded %ldms\n", calc_timediff(&snapshot_stop, &jpeg_encoded));
    LOG1("start preview %ldms\n", calc_timediff(&jpeg_encoded, &preview_start));
#endif
}

status_t CameraHardware::startPreview()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int fd;
#ifdef PERFORMANCE_TUNING
    gettimeofday(&preview_start, 0);
    print_snapshot_time();
#endif
    if (mCaptureInProgress) {
        LogError("capture in progress, not allowed");
        return INVALID_OPERATION;
    }

    mPreviewLock.lock();
    if (mPreviewRunning) {
        LogError("preview thread already running");
        mPreviewLock.unlock();
        return INVALID_OPERATION;
    }

    if (mExitPreviewThread) {
        LogError("preview thread does not exists");
        mPreviewLock.unlock();
        return INVALID_OPERATION;
    }
    setSkipFrame(mPreviewSkipFrame);

    //Enable the preview 3A
    if (mSensorType == SENSOR_TYPE_RAW) {
        mAeAfAwbLock.lock();
        mPreviewAeAfAwbRunning = true;
        mAeAfAwbLock.unlock();
        mPreviewAeAfAwbCondition.signal();
        mAAA->SetAfEnabled(true);
    }

    //Determine which preview we are in
    if (mVideoPreviewEnabled) {
        int w, h, size, padded_size;
        LogDetail("Start recording preview");
        mRecordingFrame = 0;
        mPostRecordingFrame = 0;
        mCamera->getRecorderSize(&w, &h, &size, &padded_size);
        initRecordingBuffer(size, padded_size);
        fd = mCamera->startCameraRecording();
        if (fd >= 0) {
            if (mCamera->getDVS()) {
                mAAA->SetDoneStatisticsState(false);
                LogDetail("dvs, line:%d, signal thread", __LINE__);
                mDvsCondition.signal();
            }
        }
    } else {
        LogDetail("Start normal preview");
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
        LogError("Fail on mCamera->startPreview()");
        return -1;
    }

    mPreviewRunning = true;
    mPreviewLock.unlock();
    mPreviewCondition.signal();

    mAAA->SetAfEnabled(true);
    mAAA->SetAeEnabled(true);
    mAAA->SetAwbEnabled(true);

    return NO_ERROR;
}

void CameraHardware::stopPreview(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    // request that the preview thread stop.
    if (!mPreviewRunning) {
        LogDetail("preview not running, doing nothing");
        return ;
    }
    mAAA->SetAfEnabled(false);
    mAAA->SetAeEnabled(false);
    mAAA->SetAwbEnabled(false);
    if(!mExitAutoFocusThread) {
        cancelAutoFocus();
    }
    //Waiting for the 3A to stop if it is running
    if (mSensorType == SENSOR_TYPE_RAW) {
        mAeAfAwbLock.lock();
        if (mPreviewAeAfAwbRunning) {
            LogDetail("Waiting for 3A to finish");
            mPreviewAeAfAwbRunning = false;
            mPreviewFrameCondition.signal();
            mAeAfAwbEndCondition.wait(mAeAfAwbLock);
        }
        mAeAfAwbLock.unlock();

        LogDetail("Stopped the 3A now");
    }
    //Tell preview to stop
    mPreviewRunning = false;

    mPreviewLock.lock();
    if(mVideoPreviewEnabled) {
        mCamera->stopCameraRecording();
        deInitRecordingBuffer();
    } else {
        mCamera->stopCameraPreview();
    }
    mPreviewLock.unlock();
}

bool CameraHardware::previewEnabled()
{
    return mPreviewRunning;
}

#if ENABLE_BUFFER_SHARE_MODE
int CameraHardware::getSharedBuffer()
{
    LogEntry(LOG_TAG, __FUNCTION__);
   /* block until get the share buffer information*/
   if ((!isVideoStarted) && mRecordRunning) {
       int bufferCount;
       unsigned char *pointer;
       SharedBufferType *pSharedBufferInfoArray = NULL;
       android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());

       LogDetail("camera try to get share buffer array information");
       r->sourceEnterSharingMode();
       r->sourceGetSharedBuffer(NULL, &bufferCount);

       pSharedBufferInfoArray = (SharedBufferType *)malloc(sizeof(SharedBufferType) * bufferCount);
       if(!pSharedBufferInfoArray) {
           LogError("pShareBufferInfoArray malloc failed!");
           return -1;
       }

       r->sourceGetSharedBuffer(pSharedBufferInfoArray, &bufferCount);
       LogDetail("camera have already gotten share buffer array information");

       if(bufferCount > PREVIEW_NUM_BUFFERS) {
           bufferCount = PREVIEW_NUM_BUFFERS;
       }

       unsigned int ptr_size = sizeof(unsigned char*);

       for(int i = 0; i < bufferCount; i ++) {
          mRecordingBuffer.pointerArray[i] = pSharedBufferInfoArray[i].pointer;
          LogDetail("pointer[%d] = %p (%dx%d - stride %d) ", i,
               mRecordingBuffer.start[i], pSharedBufferInfoArray[i].width,
               pSharedBufferInfoArray[i].height,
               pSharedBufferInfoArray[i].stride);
          //Initialize the mUserptrMem again with new userptr
          memcpy(mUserptrMem[i]->data, &mRecordingBuffer.pointerArray[i], ptr_size);
          memset(mRecordingBuffer.pointerArray[i], 1, mRecorderFrameSize);
       }

       if (mCamera->updateRecorderUserptr(bufferCount,
                            (unsigned char **)mRecordingBuffer.pointerArray) < 0) {
           LogError("update recorder userptr failed");
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
    LogEntry(LOG_TAG, __FUNCTION__);
   /* check whether encoder have send signal to stop buffer sharing mode.*/
   if(isCameraTurnOffBufferSharingMode) {
       LogDetail("isCameraTurnOffBufferSharingMode == true");
       return true;
    }

    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());

    if(!isCameraTurnOffBufferSharingMode
        && false == r->isBufferSharingModeSet()) {
        LogDetail("buffer sharing mode has been turned off,"
             "now de-reference pointer");
        mCamera->updateRecorderUserptr(PREVIEW_NUM_BUFFERS, (unsigned char **)mRecordingBuffer.start);
        r->sourceExitSharingMode();

        isCameraTurnOffBufferSharingMode = true;

        return true;
    }
   return false;
}

bool CameraHardware::requestEnableSharingMode()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    isVideoStarted = false;
    isCameraTurnOffBufferSharingMode = false;
    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());
    return (r->sourceRequestToEnableSharingMode() == BS_SUCCESS?true:false);
}

bool CameraHardware::requestDisableSharingMode()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    isVideoStarted = false;
    isCameraTurnOffBufferSharingMode = true;
    android::sp<BufferShareRegistry> r = (android::BufferShareRegistry::getInstance());
    return (r->sourceRequestToDisableSharingMode() == BS_SUCCESS? true:false);
}
#endif
status_t CameraHardware::startRecording()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    Mutex::Autolock lock(mRecordLock);

    for (int i=0; i < PREVIEW_NUM_BUFFERS; i++) {
        clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
    }

    mRecordRunning = true;
    if (CAM_AE_FLASH_MODE_TORCH == mCamera->getFlashMode())
        mCamera->enableTorch(TORCH_INTENSITY);
    else if (CAM_AE_FLASH_MODE_OFF == mCamera->getFlashMode())
        mCamera->enableIndicator(INDICATOR_INTENSITY);
#if ENABLE_BUFFER_SHARE_MODE
    requestEnableSharingMode();
#endif
    return NO_ERROR;
}

void CameraHardware::stopRecording()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    Mutex::Autolock lock(mRecordLock);
    mRecordRunning = false;
    if (CAM_AE_FLASH_MODE_TORCH == mCamera->getFlashMode())
        mCamera->enableTorch(0);
    else if (CAM_AE_FLASH_MODE_OFF == mCamera->getFlashMode())
        mCamera->enableIndicator(0);

#if ENABLE_BUFFER_SHARE_MODE
    requestDisableSharingMode();
#endif
}

bool CameraHardware::recordingEnabled()
{
    return mRecordRunning;
}

void CameraHardware::releaseRecordingFrame(const void* mem)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    // check if IMemory is NULL
    camera_memory_t* frameToRelease = (camera_memory_t*)mem;
    if (frameToRelease == NULL || frameToRelease->data == NULL) {
        LogError("mem is NULL");
        return;
    }

    ssize_t offset = (ssize_t)frameToRelease->data - (ssize_t)mRecordingBuffer.mem->data;
    int releasedFrame = offset / mRecordingBuffer.baseSize;
    LogDetail("a recording frame transfered to client has been released, index %d",
         releasedFrame);

    clrBF(&mRecordingBuffer.flags[releasedFrame], BF_LOCKED);

}

// ---------------------------------------------------------------------------

status_t CameraHardware::autoFocus()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    //signal autoFocusThread to run once
    mExitAutoFocusThread = false;
    mAutoFocusThread->run("CameraAutoFocusThread", PRIORITY_DEFAULT);
    return NO_ERROR;
}

status_t CameraHardware::cancelAutoFocus()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mSensorType == SENSOR_TYPE_SOC)
        return NO_ERROR;

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
    LogEntry(LOG_TAG, __FUNCTION__);
    return NO_ERROR;
}

status_t CameraHardware::cancelTouchToFocus()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    return cancelAutoFocus();
}

/* Return true, the thread will loop. Return false, the thread will terminate. */
int CameraHardware::dvsThread()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int dvs_en;

    mDvsMutex.lock();
    LogDetail("dvs, line:%d, before mDvsCondition", __LINE__);
    mDvsCondition.wait(mDvsMutex);
    LogDetail("dvs, line:%d, after mDvsCondition", __LINE__);
    mDvsMutex.unlock();

    if (mExitDvsThread) {
        LogDetail("dvs, line:%d, return false from dvsThread", __LINE__);
        return false;
    }

    while (mVideoPreviewEnabled ) {
        if (mExitDvsThread) {
            LogDetail("dvs, line:%d, return false from dvsThread", __LINE__);
            return false;
        }
        if (mCamera->getDVS()) {
            LogDetail("dvs, line:%d, read statistics from isp driver", __LINE__);
            mAAA->DvsProcess();
        } else {
            LogDetail("dvs, line:%d, get DVS false in the dvsThread", __LINE__);
            return true;
        }
    }

    LogDetail("dvs, line:%d, return true from dvsThread", __LINE__);
    return true;
}

void CameraHardware::exifAttributeOrientation(exif_attribute_t& attribute)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    // the orientation information will pass from the application. here map it.
    // relative relationship between gsensor orientation and sensor's angle.
    int rotation = mParameters.getInt(CameraParameters::KEY_ROTATION);
    struct camera_info cam_info;
    attribute.orientation = 1;
    getCameraInfo(mCameraId, &cam_info);
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
            attribute.orientation = 8;
    }
    LogDetail("exifAttribute, sensor angle:%d degrees, rotation value:%d degrees, orientation value:%d",
        cam_info.orientation, rotation, attribute.orientation);
}

void CameraHardware::exifAttributeGPS(exif_attribute_t& attribute)
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogDetail("gps_en: %d", gps_en);

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
        LogDetail("latitude, ref:%s, dd:%d, mm:%d, ss:%d",
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
        LogDetail("longitude, ref:%s, dd:%d, mm:%d, ss:%d",
            attribute.gps_longitude_ref, attribute.gps_longitude[0].num,
            attribute.gps_longitude[1].num, attribute.gps_longitude[2].num);

        // altitude, see level or above see level, set it to 0; below see level, set it to 1
        altitude = fabs(atof(paltitude));
        attribute.gps_altitude_ref = ((atol(paltitude) > 0) ? 0 : 1);
        attribute.gps_altitude.num = (uint32_t)altitude;
        attribute.gps_altitude.den = 1;
        LogDetail("altitude, ref:%d, height:%d",
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
        LogDetail("timestamp, year:%d,mon:%d,day:%d,hour:%d,min:%d,sec:%d",
            time.tm_year, time.tm_mon, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);

        // processing method
        if(strlen(pprocmethod) + 1 >= sizeof(attribute.gps_processing_method))
            len = sizeof(attribute.gps_processing_method);
        else
            len = strlen(pprocmethod) + 1;
        memcpy(attribute.gps_processing_method, pprocmethod, len);
        LogDetail("proc method:%s", attribute.gps_processing_method);
    }
}

// handle the exif tags data
void CameraHardware::exifAttribute(exif_attribute_t& attribute, int cap_w, int cap_h,
                                                                            bool thumbnail_en, bool flash_en)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ae_mode;
    unsigned short exp_time, aperture;
    int ret;
    unsigned int focal_length, fnumber;

    // get data from driver
    mCamera->acheiveEXIFAttributesFromDriver();

    memset(&attribute, 0, sizeof(attribute));
    // exp_time's unit is 100us
    mAAA->AeGetExpCfg(&exp_time, &aperture);
    LogDetail("exptime:%d, aperture:%d", exp_time, aperture);

    attribute.enableThumb = thumbnail_en;
    LogDetail("thumbnal:%d", thumbnail_en);

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
    attribute.exposure_time.num = exp_time;
    attribute.exposure_time.den = 10000;

    // shutter speed, = -log2(exposure time)
    float exp_t = (float)(exp_time / 10000.0);
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
        LogDetail("fnumber:%x, num: %d, den: %d", fnumber, attribute.fnumber.num, attribute.fnumber.den);
    }

    // aperture
    attribute.aperture.num = (int)((((double)attribute.fnumber.num/(double)attribute.fnumber.den)* sqrt(100.0/aperture))*100);
    attribute.aperture.den = 100;

    // conponents configuration. 0 means does not exist
    // 1 = Y; 2 = Cb; 3 = Cr; 4 = R; 5 = G; 6 = B; other = reserved
    memset(attribute.components_configuration, 0, sizeof(attribute.components_configuration));


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
    LogDetail("mPostViewWidth:%d, mPostViewHeight:%d",
        mPostViewWidth, mPostViewHeight);

    exifAttributeOrientation(attribute);

    // the TIFF default is 1 (centered)
    attribute.ycbcr_positioning = EXIF_DEF_YCBCR_POSITIONING;

    if (mSensorType == SENSOR_TYPE_RAW) {
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
        LogDetail("brightness:%f, ev:%f", brightness, bias);

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
            LogDetail("AeGetManualIso failed!");
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

        // exposure mode settting. 0: auto; 1: manual; 2: auto bracket; other: reserved
        if (AAA_SUCCESS == mAAA->AeGetMode(&ae_mode)) {
            LogDetail("exifAttribute, ae mode:%d success", ae_mode);
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
    }

    // bit 0: flash fired; bit 1 to 2: flash return; bit 3 to 4: flash mode;
    // bit 5: flash function; bit 6: red-eye mode;
    attribute.flash = (flash_en ? EXIF_FLASH_ON : EXIF_DEF_FLASH);

    // normally it is sRGB, 1 means sRGB. FFFF.H means uncalibrated
    attribute.color_space = EXIF_DEF_COLOR_SPACE;

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
        LogDetail("line:%d, focal_length:%x, num: %d, den: %d", __LINE__, focal_length, attribute.focal_length.num, attribute.focal_length.den);
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

int CameraHardware::snapshotSkipFrames(void **main, void **postview)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int index;

    while (mSkipFrame) {
        index = mCamera->getSnapshot(main, postview, NULL, NULL);
        if (index < 0) {
            LogError("line:%d, getSnapshot fail", __LINE__);
            return -1;
        }
        if (mCamera->putSnapshot(index) < 0) {
            LogError("line:%d, putSnapshot fail", __LINE__);
            return -1;
        }
        mSkipFrame--;
    }

    return 0;
}

/* Return true, the thread will loop. Return false, the thread will terminate. */
int CameraHardware::compressThread()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    struct BCBuffer *bcbuf;
    int cap_w, cap_h;
    int rgb_frame_size, jpeg_buf_size;
    void *pexif;
    void *pmainimage;
    void *pthumbnail;   // first save RGB565 data, then save jpeg encoded data into this pointer
    int i, j, ret;

    if (mBCCancelCompress) {
        LogDetail("BC, line:%d, mBCCancelCompress is true, terminate", __LINE__);
        mBCCancelCompress = false;
        return false;
    }

    mCompressLock.lock();
    LogDetail("BC, line:%d, before receive mCompressCondition", __LINE__);
    mCompressCondition.wait(mCompressLock);
    LogDetail("BC, line:%d, received mCompressCondition", __LINE__);
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
        LogDetail("main_quality:%d, thumbnail_quality:%d", main_quality, thumbnail_quality);

        mCamera->getSnapshotSize(&cap_w, &cap_h, &rgb_frame_size);

        if (mHwJpegBufferShareEn && (mPicturePixelFormat == V4L2_PIX_FMT_NV12)) {
            //set parameter for jpeg encode
            mBCLibJpgHw->setJpeginfo(cap_w, cap_h, 3, JCS_YCbCr, main_quality);
            if(mBCLibJpgHw->preStartJPEGEncodebyHwBufferShare() < 0) {
                LogError("BC, line:%d, call startJPEGEncodebyHwBufferShare failed!", __LINE__);
                return false;
            }
        }

        // loop and wait to hande all the pictures
        for (i = 0; i < mBCNumReq; i++) {
            if (mBCCancelCompress) {
                LogDetail1("BC, line:%d, int compressThread, mBCCancelCompress is true, terminate", __LINE__);
                mBCCancelCompress = false;
                return false;
            }
            LogDetail("BC, line:%d, before sem_wait:sem_bc_captured, %d", __LINE__, i);
            if ((ret = sem_wait(&sem_bc_captured)) < 0)
                LogError("BC, line:%d, sem_wait fail, ret:%d", __LINE__, ret);
            bcbuf = mBCBuffer;
            for (j = 0; j < mBCNumReq; j++) {
                bcbuf = mBCBuffer + j;
                if (bcbuf->sequence == i)
                    break;
            }
            if (mBCCancelCompress) {
                LogDetail("BC, line:%d, int compressThread, mBCCancelCompress is true, terminate", __LINE__);
                mBCCancelCompress = false;
                return false;
            }
            if (j == mBCNumReq) {
                LogError("BC, line:%d, error, j:%d == mBCNumReq", __LINE__, j);
                return false;
            }
            LogDetail("BC, line:%d, after sem_wait:sem_bc_captured, i:%d, j:%d", __LINE__, i, j);

            pexif = bcbuf->pdst_exif;
            pthumbnail = bcbuf->pdst_thumbnail;
            pmainimage = bcbuf->pdst_main;

            if (mHwJpegBufferShareEn && (mPicturePixelFormat == V4L2_PIX_FMT_NV12)) {
                // do jpeg encoded
                if(mBCLibJpgHw->startJPEGEncodebyHwBufferShare(bcbuf->usrptr) < 0){
                    mainimage_size = 0;
                    LogError("BC, line:%d, call startJPEGEncodebyHwBufferShare fail", __LINE__);
                } else {
                    mainimage_size = mBCLibJpgHw->getJpegSize();
                    if(mainimage_size > 0){
                        memcpy(pmainimage, mBCHwJpgDst, mainimage_size);
                        // write_image(pmainimage, mainimage_size, 3264, 2448, "snap_v0.jpg");
                    } else {
                        LogError("BC, line:%d, mainimage_size:%d", __LINE__, mainimage_size);
                    }
                    LogDetail("BC, line:%d, mainimage_size:%d", __LINE__, mainimage_size);
                }
            } else {
                // get RGB565 main data from NV12/YUV420
                mCamera->toRGB565(cap_w, cap_h, mPicturePixelFormat, bcbuf->psrc, bcbuf->psrc);
                // encode the main image
                if (encodeToJpeg(cap_w, cap_h, bcbuf->psrc, pmainimage, &mainimage_size, main_quality) < 0) {
                    LogError("BC, line:%d, encodeToJpeg fail for main image", __LINE__);
                }
            }

            // encode the thumbnail
            void *pdst = pthumbnail;
            if (encodeToJpeg(mPostViewWidth, mPostViewHeight, pthumbnail, pdst, &thumbnail_size, thumbnail_quality) < 0) {
                LogError("BC, line:%d, encodeToJpeg fail for main image", __LINE__);
            }
            memcpy(pdst, FILE_START, sizeof(FILE_START));

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
            LogDetail("exif sz:0x%x,thumbnail sz:0x%x,main sz:0x%x", exif_size, thumbnail_size, mainimage_size);

            // move data together
            void *pjpg_start = pexif;
            void *pjpg_exifend = (void*)((char*)pjpg_start + sizeof(FILE_START) + exif_size);
            void *pjpg_main = (void*)((char*)pjpg_exifend + sizeof(FILE_END));
            void *psrc = (void*)((char*)pmainimage + sizeof(FILE_START));
            memcpy(pjpg_start, FILE_START, sizeof(FILE_START));
            memcpy(pjpg_exifend, FILE_END, sizeof(FILE_END));
            memmove(pjpg_main, psrc, mainimage_size - sizeof(FILE_START));

            jpeg_file_size =sizeof(FILE_START) + exif_size + sizeof(FILE_END) + mainimage_size - sizeof(FILE_START);
            LogDetail("jpg file sz:%d", jpeg_file_size);

            bcbuf->encoded = true;
            bcbuf->jpeg_size = jpeg_file_size;

            // post sem to let the picture thread to send the jpeg pic out
            if ((ret = sem_post(&sem_bc_encoded)) < 0)
                LogError("BC, line:%d, sem_post fail, ret:%d", __LINE__, ret);
            LogDetail("BC, line:%d, encode:%d finished, sem_post", __LINE__, i);
        }

        if (i == mBCNumReq) {
            LogDetail("BC, line:%d, leave compressThread", __LINE__);
            return false;
        }
    }

    return true;
}

void CameraHardware::burstCaptureInit(bool init_flags)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    mBCNumCur = 0;
    mBCEn = false;
    mBCNumReq = 1;
    mBCNumSkipReq = 0;
    mBCBuffer = NULL;
    mBCHeap = NULL;

    if (init_flags) {
        mBCCancelCompress = false;
        mBCCancelPicture = false;
        mBCMemState = false;
        mBCDeviceState = false;
    }
}

int CameraHardware::burstCaptureAllocMem(int total_size, int rgb_frame_size,
                        int cap_w, int cap_h, int jpeg_buf_size, void *postview_out)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    struct BCBuffer *bcbuf;
    int i;
    void *usrptr[MAX_BURST_CAPTURE_NUM];
    memset(usrptr, 0, sizeof(usrptr));

    if (mBCNumReq > MAX_BURST_CAPTURE_NUM) {
        LogError("BC, line:%d, mBCNumReq > MAX_BURST_CAPTURE_NUM", __LINE__);
        return -1;
    }

    if (mHwJpegBufferShareEn && (mPicturePixelFormat == V4L2_PIX_FMT_NV12)) {
        mBCHeapHwJpgDst = mGetMemory(-1, jpeg_buf_size, 1, NULL);
        if (mBCHeapHwJpgDst->data == NULL || mBCHeapHwJpgDst->size <= 0) {
            LogError("BC, line:%d, mBCHeap fail", __LINE__);
            return -1;
        }
        mBCHwJpgDst = mBCHeapHwJpgDst->data;

        mBCLibJpgHw = NULL;
        mBCLibJpgHw = new HWLibjpegWrap();
        if (mBCLibJpgHw == NULL) {
            LogError("BC, line:%d, new HWLibjpegWrap fail", __LINE__);
            mBCHeapHwJpgDst->release(mBCHeapHwJpgDst);
            return -1;
        }

        if(mBCLibJpgHw->initHwBufferShare((JSAMPLE *)mBCHwJpgDst,
            jpeg_buf_size,cap_w,cap_h,(void**)&usrptr, mBCNumReq) != 0) {
            LogError("BC, line:%d, initHwBufferShare fail", __LINE__);
            mBCHeapHwJpgDst->release(mBCHeapHwJpgDst);
            delete mBCLibJpgHw;
            mBCLibJpgHw = NULL;
            return -1;
        }

        for (i = 0; i < mBCNumReq; i++) {
            LogDetail("BC, line:%d, usrptr[%d]:0x%x", __LINE__, i, (int)(usrptr[i]));
            if (usrptr[i] == NULL) {
                mBCHeapHwJpgDst->release(mBCHeapHwJpgDst);
                delete mBCLibJpgHw;
                mBCLibJpgHw = NULL;
                return -1;
            }
        }
    }

    mBCHeap = mGetMemory(-1, sizeof(struct BCBuffer), mBCNumReq, NULL);
    if (mBCHeap->data == NULL || mBCHeap->size <= 0) {
        LogError("BC, line:%d, mBCHeap fail", __LINE__);
        mBCHeapHwJpgDst->release(mBCHeapHwJpgDst);
        delete mBCLibJpgHw;
        mBCLibJpgHw = NULL;
        return -1;
    }
    mBCBuffer = (struct BCBuffer *)mBCHeap->data;
    for (i = 0; i < mBCNumReq; i++) {
        bcbuf = mBCBuffer + i;

        bcbuf->mem = mGetMemory(-1, total_size, 1, NULL);
        if (bcbuf->mem->data == NULL || bcbuf->mem->size <= 0) {
            LogError("BC, line:%d, malloc heap fail, i:%d", __LINE__, i);
            mBCHeapHwJpgDst->release(mBCHeapHwJpgDst);
            delete mBCLibJpgHw;
            mBCLibJpgHw = NULL;
            mBCHeap->release(mBCHeap);
            for (int j = 0; j < i; j++) {
                struct BCBuffer *bcbuf;
                bcbuf = mBCBuffer + j;
                bcbuf->mem->release(bcbuf->mem);
            }
            return -1;
        }

        bcbuf->total_size = total_size;
        bcbuf->src_size = rgb_frame_size;

        bcbuf->jpeg_size = 0;

        bcbuf->psrc = bcbuf->mem->data;
        bcbuf->pdst_exif = (char *)bcbuf->psrc + bcbuf->src_size;
        bcbuf->pdst_thumbnail = (char *)bcbuf->pdst_exif + exif_offset;
        bcbuf->pdst_main = (char *)bcbuf->pdst_thumbnail + thumbnail_offset;

        bcbuf->ready = false;
        bcbuf->encoded = false;
        bcbuf->sequence = ~0;

        bcbuf->usrptr = usrptr[i];

        if (memory_userptr) {
            if (mHwJpegBufferShareEn && (mPicturePixelFormat == V4L2_PIX_FMT_NV12)) {
                mCamera->setSnapshotUserptr(i, bcbuf->usrptr, postview_out);
            } else {
                mCamera->setSnapshotUserptr(i, bcbuf->psrc, postview_out);
            }
        }
    }

    mBCMemState = true;
    return 0;
}

void CameraHardware::burstCaptureFreeMem(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    struct BCBuffer *bcbuf;
    int i;

    if (mBCMemState == false)
        return;

    for (i = 0; i < mBCNumReq; i++) {
        bcbuf = mBCBuffer + i;
        bcbuf->mem->release(bcbuf->mem);
    }
    mBCHeap->release(mBCHeap);

    if (mHwJpegBufferShareEn && (mPicturePixelFormat == V4L2_PIX_FMT_NV12)) {
        LogDetail("BC, line:%d, i:%d, before delete mBCLibJpgHw", __LINE__, i);
        if (mBCLibJpgHw)
            delete mBCLibJpgHw;

        mBCHeapHwJpgDst->release(mBCHeapHwJpgDst);
    }

    mBCMemState = false;
}

// open the device
int CameraHardware::burstCaptureStart(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret;
    ret = mCamera->startSnapshot();
    if (ret < 0)
        return ret;
    update3Aresults();
    mBCDeviceState = (ret < 0) ? false : true;
    return ret;
}

// close the device
void CameraHardware::burstCaptureStop(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mBCDeviceState == false)
        return;
    mCamera->stopSnapshot();
    mCaptureInProgress = false;
    mBCDeviceState = false;
}

int CameraHardware::burstCaptureSkipReqBufs(int i, int *idx, void **main, void **postview)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int skipped, index = 0;
    void *main_out, *postview_out;

    for (skipped = 0; skipped <= mBCNumSkipReq; skipped++) {
        if (mBCCancelPicture)
            return -1;

        //dq buffer
        index = mCamera->getSnapshot(&main_out, &postview_out, NULL, NULL);
        if (index < 0) {
            LogError("BC, line:%d, getSnapshot fail", __LINE__);
            return -1;
        }
        if (i == 0) { // we don't need to skip the first frame
            LogDetail("BC, line:%d, dq buffer, i:%d", __LINE__, i);
            break;
        }

        if(skipped < mBCNumSkipReq) {
            mCamera->putSnapshot(index);
            LogDetail("BC, line:%d, skipped dq buffer, i:%d", __LINE__, i);
        }
        else
            LogDetail("BC, line:%d, dq buffer, i:%d", __LINE__, i);
    }

    *idx = index;
    *main = main_out;
    *postview = postview_out;
    return 0;
}

// the handling at the time of cancel picture
void CameraHardware::burstCaptureCancelPic(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    burstCaptureStop();
    burstCaptureFreeMem();
    mBCCancelPicture = false;
    mCaptureInProgress = false;
}

// call from pictureThread
int CameraHardware::burstCaptureHandle(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int fd, i, index = 0, ret;
    int cap_w, cap_h;
    int rgb_frame_size, jpeg_buf_size, total_size;
    int skipped;
    void *main_out, *postview_out;
    struct BCBuffer *bcbuf;
    camera_memory_t* JpegBuffer;

    // get size
    mCamera->getPostViewSize(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    mPostViewFormat = mCamera->getPostViewPixelFormat();
    mCamera->getSnapshotSize(&cap_w, &cap_h, &rgb_frame_size);
    if (mHwJpegBufferShareEn && (mPicturePixelFormat == V4L2_PIX_FMT_NV12))
        rgb_frame_size = 0;
    else
        rgb_frame_size = cap_w * cap_h * 2; // for RGB565 format
    jpeg_buf_size = cap_w * cap_h * 3 / 10; // for store the encoded jpeg file, experience value
    total_size = rgb_frame_size + exif_offset + thumbnail_offset + jpeg_buf_size;

    // the first time of calling taking picture
    if (mBCNumCur == 1) {
        //get postview's memory base
        postview_out = mRawMem->data;

        // allocate memory
        if (burstCaptureAllocMem(total_size, rgb_frame_size,
                                cap_w, cap_h, jpeg_buf_size, postview_out) < 0) {
            goto BCHANDLE_ERR;
        }

        //Prepare for the snapshot
        if ((fd = burstCaptureStart()) < 0) {
            LogError("BC, line:%d, burstCaptureStart fail", __LINE__);
            goto BCHANDLE_ERR;
        }

        //Skip frames
        snapshotSkipFrames(&main_out, &postview_out);
        for (i = 0; i < mBCNumReq; i++) {
            if (mBCCancelPicture) {
                LogDetail("BC, line:%d, in burstCaptureHandle, mBCCancelPicture is true, terminate", __LINE__);
                burstCaptureCancelPic();
                return NO_ERROR;
            }

            // dq buffer and skip request buffer
            if (burstCaptureSkipReqBufs(i, &index, &main_out, &postview_out) < 0 ) {
                if (mBCCancelPicture) {
                    LogDetail("BC, line:%d, in burstCaptureHandle, mBCCancelPicture is true, terminate", __LINE__);
                    burstCaptureCancelPic();
                    return NO_ERROR;
                }
                goto BCHANDLE_ERR;
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
            LogDetail("BC, line:%d, shutter:%d", __LINE__, i);

            // do nothing for RAW message
            if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
                LogDetail("BC, line:%d,do nothing for CAMERA_MSG_RAW_IMAGE", __LINE__);
            }

            if (!memory_userptr) {
                if (!mHwJpegBufferShareEn)
                    memcpy(bcbuf->psrc, main_out, bcbuf->src_size);
            }

            // mark the src data ready
            bcbuf->ready = true;
            LogDetail("BC, line:%d, index:%d, ready:%d, sequence:%d", __LINE__, index, bcbuf->ready, bcbuf->sequence);

            // active the compress thread
            if (i == 0) {
                LogDetail("BC, line:%d, send the signal to compressthread", __LINE__);
                mCompressCondition.signal();
            }

            // let the compress thread to encode the jpeg
            LogDetail("BC, line:%d, before sem_post:sem_bc_captured, %d", __LINE__, i);
            if ((ret = sem_post(&sem_bc_captured)) < 0)
                LogError("BC, line:%d, sem_post fail, ret:%d", __LINE__, ret);
            LogDetail("BC, line:%d, after sem_post:sem_bc_captured, %d", __LINE__, i);

            //Postview
            LogDetail("Sending message: CAMERA_MSG_POSTVIEW_FRAME");
            mDataCb(CAMERA_MSG_POSTVIEW_FRAME, mRawMem, 0, NULL, mCallbackCookie);
        }
        LogDetail("BC, line:%d, finished capture", __LINE__);
    }

    // find and wait the desired buffer
    bcbuf = mBCBuffer;
    for (i = 0; i < mBCNumReq; i++) {
        bcbuf = mBCBuffer + i;
        if ((bcbuf->sequence + 1) == mBCNumCur) {
            if ((ret = sem_wait(&sem_bc_encoded)) < 0)
                LogError("BC, line:%d, sem_wait fail, ret:%d", __LINE__, ret);
            LogDetail("BC, line:%d, sem_wait sem_bc_encoded, i:%d", __LINE__, i);
            break;
        }
    }
    if (i == mBCNumReq) {
        LogError("BC, line:%d, error, i:%d == mBCNumReq", __LINE__, i);
        goto BCHANDLE_ERR;
    }

    if (mBCCancelPicture) {
        LogDetail("BC, line:%d, in burstCaptureHandle, mBCCancelPicture is true, terminate", __LINE__);
        burstCaptureCancelPic();
        return NO_ERROR;
    }

    if (mBCNumCur == mBCNumReq) {
        LogDetail("BC, line:%d, begin to stop the camera", __LINE__);
        burstCaptureStop();
        //Set captureInProgress earlier.
        mCaptureInProgress = false;
    }

    // send compressed jpeg image to upper
    JpegBuffer = mGetMemory(-1, bcbuf->jpeg_size, 1, (void*)((unsigned)bcbuf->mem->data + bcbuf->src_size));
    LogDetail("Sending message: CAMERA_MSG_COMPRESSED_IMAGE");
    mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, JpegBuffer, 0, NULL, mCallbackCookie);
    JpegBuffer->release(JpegBuffer);
    LogDetail("BC, line:%d, send the %d, compressed jpeg image", __LINE__, i);

    mCaptureInProgress = false;

    if (mBCNumCur == mBCNumReq) {
        LogDetail("BC, line:%d, begin to clean up the memory", __LINE__);
        // release the memory
        burstCaptureFreeMem();
        burstCaptureInit(false);
    }

    return NO_ERROR;

BCHANDLE_ERR:
    LogError("BC, line:%d, got BCHANDLE_ERR in the burstCaptureHandle", __LINE__);
    burstCaptureStop();
    burstCaptureFreeMem();
    mCaptureInProgress = false;

    mNotifyCb(CAMERA_MSG_ERROR, CAMERA_ERROR_UNKNOWN, 0, mCallbackCookie);

    return UNKNOWN_ERROR;
}

#define MAX_FRAME_WAIT 3
#define FLASH_FRAME_WAIT 4
int CameraHardware::pictureThread()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    bool flash = false;
    int cnt = 0;
    int cap_width, cap_height, cap_frame_size, rgb_frame_size;
    int pre_width, pre_height, pre_frame_size, pre_padded_size;
    mCamera->getSnapshotSize(&cap_width, &cap_height, &cap_frame_size);
    mCamera->getPreviewSize(&pre_width, &pre_height, &pre_frame_size, &pre_padded_size);

    //Postview size should be smaller
    mCamera->setPostViewSize(pre_width>>1, pre_height>>1, V4L2_PIX_FMT_NV12);

    // ToDo. abstract some functions for both single capture and burst capture.
    if (mBCEn) {
        mCamera->setSnapshotNum(mBCNumReq);
        mBCNumCur++;
        LogDetail("BC, line:%d, BCEn:%d, BCReq:%d, BCCur:%d", __LINE__, mBCEn, mBCNumReq, mBCNumCur);
        if (mBCNumCur == 1) {
            mBCCancelPicture = false;
            if (mCompressThread->run("CameraCompressThread", PRIORITY_DEFAULT) !=
            NO_ERROR) {
                LogError("couldn't run compress thread");
                return INVALID_OPERATION;
            }
        }
        return burstCaptureHandle();
    } else {
        mCamera->setSnapshotNum(1);
    }

    int af_mode;
    mAAA->AfGetMode (&af_mode);
    // compute if flash should on/off for auto focus thread has not run
    if (af_mode == CAM_AF_MODE_INFINITY ||
        af_mode == CAM_AF_MODE_MANUAL) {
        calculateLightLevel();
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
    void *pmainimage;
    void *pthumbnail;   // first save RGB565 data, then save jpeg encoded data into this pointer
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
    HWLibjpegWrap libjpghw;
    void* usrptr[1];//you can set usrptr[num] in order to get num usrptr
    bool bHwEncodepath=TRUE;
    //Although enable hwlibjpeg buffer share, if picture resolution is below 640*480, we have to go software path
    if(V4L2_PIX_FMT_YUV420 == mPicturePixelFormat)
        bHwEncodepath=FALSE;
#endif
    mCamera->getPostViewSize(&mPostViewWidth, &mPostViewHeight, &mPostViewSize);
    rgb_frame_size = cap_width * cap_height * 2;

    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        int index;
        void *main_out, *postview_out;
        postview_out = mRawMem->data;
        unsigned int page_size = getpagesize();
        unsigned int capsize_aligned = (rgb_frame_size + page_size - 1)
                                              & ~(page_size - 1);
        unsigned total_size = capsize_aligned + exif_offset + thumbnail_offset;

        camera_memory_t* picMem = mGetMemory(-1, total_size, 1, NULL);
        pthumbnail = (void*)((char*)(picMem->data) + exif_offset);
        pmainimage = (void*)((char*)(picMem->data) + exif_offset + thumbnail_offset);

#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
        if(bHwEncodepath){
            //initialize buffer share with hardware libjpeg
            if(libjpghw.initHwBufferShare((JSAMPLE *)pmainimage,capsize_aligned,cap_width,cap_height,(void**)usrptr,1) != 0){
                LogDetail("initHwBufferShare failed!");
                goto start_error_out;
            }
        }
        else
            usrptr[0] =  pmainimage ;//software path, we have to set usrptr to memory allocated in camera hal
#endif

        if (memory_userptr) {
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
            mCamera->setSnapshotUserptr(0,usrptr[0], mRawMem->data);
#else
            mCamera->setSnapshotUserptr(0, pmainimage, mRawMem->data);
#endif
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&pic_thread_start,  0);
#endif
        //Prepare for the snapshot
        int fd;
        if ((fd = mCamera->startSnapshot()) < 0)
            goto start_error_out;

        //apply the AE results
        update3Aresults();

#ifdef PERFORMANCE_TUNING
        gettimeofday(&snapshot_start, 0);
#endif
        if(!mFlashNecessary)
            mCamera->enableIndicator(INDICATOR_INTENSITY);

        //Skip frames
        snapshotSkipFrames(&main_out, &postview_out);

        //Turn on flash if neccessary before the Qbuf
        if (mFlashNecessary && mPreFlashSucceeded)
            mCamera->requestFlash(1);

#ifdef PERFORMANCE_TUNING
        gettimeofday(&first_frame, 0);
#endif

        // Modified the shutter sound timing for Jpeg capture
        if (mMsgEnabled & CAMERA_MSG_SHUTTER)
            mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

        //Get the buffer and copy the buffer out
	while (1) {
                enum atomisp_frame_status stat;
                index = mCamera->getSnapshot(&main_out, &postview_out, pthumbnail, &stat);
                if (index < 0)
                        goto get_img_error;
                if (!flash)
                        break;
                if (stat == ATOMISP_FRAME_STATUS_FLASH_EXPOSED ||
                    stat == ATOMISP_FRAME_STATUS_FLASH_FAILED)
                        break;
               /* safety precaution */
               if (cnt++ == FLASH_FRAME_TIMEOUT) {
                       LogError("terminating flash capture, no flashed frame received");
                       break;
               }
                mCamera->putSnapshot(index);
        }

        if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
            ssize_t offset = exif_offset + thumbnail_offset;
            camera_memory_t* mBuffer = mGetMemory(-1, cap_frame_size, 1, (void*)((unsigned)picMem->data + offset));
            LogDetail("Sending message: CAMERA_MSG_RAW_IMAGE");
            mDataCb(CAMERA_MSG_RAW_IMAGE, mBuffer, 0, NULL, mCallbackCookie);
            mBuffer->release(mBuffer);
        }

        if (!memory_userptr) {
#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
            memcpy(usrptr[0], main_out, cap_frame_size);
#else
            memcpy(pmainimage, main_out, rgb_frame_size);
#endif
        }

#ifdef PERFORMANCE_TUNING
        gettimeofday(&second_frame, 0);
#endif

        //Postview
        LogDetail("Sending message: CAMERA_MSG_POSTVIEW_FRAME");
        mDataCb(CAMERA_MSG_POSTVIEW_FRAME, mRawMem, 0, NULL, mCallbackCookie);

#ifdef PERFORMANCE_TUNING
        gettimeofday(&postview, 0);
#endif
        mCamera->enableIndicator(0);

        //Stop the Camera Now
        mCamera->stopSnapshot();
        mCaptureInProgress = false;

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
            LogDetail("main_quality:%d, thumbnail_quality:%d", main_quality, thumbnail_quality);

#ifdef ENABLE_HWLIBJPEG_BUFFER_SHARE
            if(bHwEncodepath){
                //set parameter for jpeg encode
                libjpghw.setJpeginfo(cap_width,cap_height,3,JCS_YCbCr,main_quality);
                if(libjpghw.preStartJPEGEncodebyHwBufferShare() != 0){
                    LogDetail("preStartJPEGEncodebyHwBufferShare failed!");
                    goto get_img_error;
                }
                if(libjpghw.startJPEGEncodebyHwBufferShare(usrptr[0]) != 0){
                      LogDetail("jpeg_destroy_compress done!");
                      goto get_img_error;
                }
                if(libjpghw.getJpegSize() > 0){
                     //there should jpeg data in pmainimage now
                     LogDetail("jpeg compress size = %d !",libjpghw.getJpegSize());
                     mainimage_size = libjpghw.getJpegSize();
                }
                else{
                    LogDetail("jpeg compress failed!");
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

            memcpy(pdst, FILE_START, sizeof(FILE_START));

            // fill the attribute
            int thumbnail_w,thumbnail_h;
            thumbnail_w = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
            thumbnail_h = mParameters.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
            LogDetail("thumbnail_size %d ,exif_offset %d  thumbnail_w * thumbnail_h = %d X%d",thumbnail_size,exif_offset,thumbnail_w ,thumbnail_h);
            if (((unsigned int)thumbnail_size >= exif_offset) ||!(thumbnail_w * thumbnail_h)) {
                // thumbnail is in the exif, so the size of it must less than exif_offset
                exifAttribute(exifattribute, cap_width, cap_height, false, mFlashNecessary);
            } else {
                exifAttribute(exifattribute, cap_width, cap_height, true, mFlashNecessary);
            }

            // set thumbnail data pointer
            jpgenc.setThumbData((unsigned char *)pdst, thumbnail_size);

            // generate exif, it includes memcpy the thumbnail
            jpgenc.makeExif((unsigned char*)(picMem->data) + sizeof(FILE_START), &exifattribute, &tmp, 0);
            exif_size = (int)tmp;
            LogDetail("exif sz:%d,thumbnail sz:%d,main sz:%d", exif_size, thumbnail_size, mainimage_size);
            jpeg_file_size =sizeof(FILE_START) + exif_size + sizeof(FILE_END) + mainimage_size - sizeof(FILE_END);
            LogDetail("jpg file sz:%d", jpeg_file_size);
            camera_memory_t* JpegBuffer = mGetMemory(-1, jpeg_file_size, 1, picMem->data);

            // move data together
            void *pjpg_start = JpegBuffer->data;
            void *pjpg_exifend = (void*)((char*)pjpg_start + sizeof(FILE_START) + exif_size);
            void *pjpg_main = (void*)((char*)pjpg_exifend + sizeof(FILE_END));
            void *psrc = (void*)((char*)pmainimage+sizeof(FILE_START));
            memcpy(pjpg_start, FILE_START, sizeof(FILE_START));
            memcpy((char*)pjpg_start + sizeof(FILE_START), (char*)picMem->data + sizeof(FILE_START), exif_size);
            memcpy(pjpg_exifend, FILE_END, sizeof(FILE_END));
            memcpy(pjpg_main, psrc, mainimage_size-sizeof(FILE_START));

            mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, JpegBuffer, 0, NULL, mCallbackCookie);
            JpegBuffer->release(JpegBuffer);
        }
#ifdef PERFORMANCE_TUNING
        gettimeofday(&jpeg_encoded, 0);
#endif

        //clean up
        picMem->release(picMem);
    }
out:
    mCaptureInProgress = false;

    return NO_ERROR;

get_img_error:
    LogError("Get the snapshot error, now stoping the camera");
    mCamera->stopSnapshot();

    if (use_file_input)
        mCamera->deInitFileInput();

start_error_out:
    mCaptureInProgress = false;
    mNotifyCb(CAMERA_MSG_ERROR, CAMERA_ERROR_UNKNOWN, 0, mCallbackCookie);

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
    LogEntry(LOG_TAG, __FUNCTION__);
    SkDynamicMemoryWStream *stream = NULL;
    SkBitmap *bitmap = NULL;
    SkImageEncoder* encoder = NULL;

    stream = new SkDynamicMemoryWStream;
    if (stream == NULL) {
        LogError("No memory for stream");
        goto stream_error;
    }

    bitmap = new SkBitmap();
    if (bitmap == NULL) {
        LogError("No memory for bitmap");
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
        LogDetail("jpeg encode result:%d, size:%d", success, *jsize);
        delete stream;
        delete bitmap;
        delete encoder;
    } else {
        LogError("No memory for encoder");
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
    LogEntry(LOG_TAG, __FUNCTION__);

#ifdef PERFORMANCE_TUNING
    gettimeofday(&picture_start, 0);
#endif
    disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    if (mFlashNecessary)
        runPreFlashSequence();
    stopPreview();
#ifdef PERFORMANCE_TUNING
    gettimeofday(&preview_stop, 0);
#endif
    enableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    setSkipFrame(mSnapshotSkipFrame);
#ifdef PERFORMANCE_TUNING
    gettimeofday(&preview_stop, 0);
#endif
    if (mCaptureInProgress) {
        LogError("capture already in progress");
        return INVALID_OPERATION;
    }

    if (mPictureThread->run("CameraPictureThread", PRIORITY_DEFAULT) !=
            NO_ERROR) {
        LogError("couldn't run picture thread");
        return INVALID_OPERATION;
    }
    mCaptureInProgress = true;

    return NO_ERROR;
}

status_t CameraHardware::cancelPicture()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mBCEn) {
        mBCCancelCompress = true;
        mCompressCondition.signal();
        sem_post(&sem_bc_captured);
        mCompressThread->requestExitAndWait();
        LogDetail("BC, line:%d, int cancelPicture, after compress thread end", __LINE__);

        mBCCancelPicture = true;
        sem_post(&sem_bc_encoded);
    }

    mPictureThread->requestExitAndWait();

    if (mBCEn) {
        burstCaptureStop();
        burstCaptureFreeMem();
    }

    return NO_ERROR;
}

int CameraHardware::autoFocusThread()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int count = 0;
    int af_status = 0;

    if (mSensorType == SENSOR_TYPE_SOC) {
        if (mMsgEnabled & CAMERA_MSG_FOCUS)
            mNotifyCb(CAMERA_MSG_FOCUS, 1, 0, mCallbackCookie);
        mExitAutoFocusThread = true;
        return NO_ERROR;
    }

    //stop the preview 3A thread
    mAeAfAwbLock.lock();
    if (mPreviewAeAfAwbRunning) {
        mPreviewAeAfAwbRunning = false;
        LogDetail("waiting for 3A thread to exit");
        mAeAfAwbEndCondition.wait(mAeAfAwbLock);
    }
    mAeAfAwbLock.unlock();

    if (mExitAutoFocusThread) {
        LogDetail("exiting on request");
        return NO_ERROR;
    }

    LogDetail("begin do the autofocus");
    mAAA->SetAfEnabled(true);
    //set the mFlashNecessary
    calculateLightLevel();

    switch(mCamera->getFlashMode())
    {
        case CAM_AE_FLASH_MODE_AUTO:
            if(!mFlashNecessary) break;
        case CAM_AE_FLASH_MODE_ON:
            mCamera->enableTorch(TORCH_INTENSITY);
            break;
        case CAM_AE_FLASH_MODE_OFF:
            break;
        default:
            break;
    }

    af_status = runStillAfSequence();
    int af_mode;
    mAAA->AfGetMode(&af_mode);

    mCamera->enableTorch(0);
    mAAA->SetAfEnabled(false);
    if (af_status == FOCUS_CANCELLED) {
        mExitAutoFocusThread = true;
        return NO_ERROR;
    }

    if(CAM_AF_MODE_TOUCH == af_mode) {
        mPreviewAeAfAwbRunning = true;
        mPreviewAeAfAwbCondition.signal();
    }

    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, af_status , 0, mCallbackCookie);
    LogDetail("exiting with no error");
    mExitAutoFocusThread = true;
    return NO_ERROR;
}

int CameraHardware::runStillAfSequence(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    //The preview thread is stopped at this point
    bool af_status = false;
    struct timeval currentTime, stillafstarttime;
    int i = 0;

    mAAA->AeLock(true);
    mAAA->AfStillStart();
    gettimeofday(&stillafstarttime,0);

    //The limit of AF process time is up to mStillAfMaxTimeMs + one frame interval
    do {
        mAeAfAwbLock.lock();
        //check whether exit before wait.
        if (mExitAutoFocusThread) {
            LogDetail("exiting on request");
            mAeAfAwbLock.unlock();
            return FOCUS_CANCELLED;//cancel
        }

        mPreviewFrameCondition.wait(mAeAfAwbLock);
        LogDetail("still AF return from wait");
        mAeAfAwbLock.unlock();
        /* TODO: need to fix this!
        if (mAAA->AeAfAwbProcess(true) < 0) {
            //mNotifyCb(CAMERA_MSG_ERROR, CAMERA_ERROR_UKNOWN, 0, mCallbackCookie);
            LOGW("%s: 3A return error", __FUNCTION__);
        }
        */
        mAAA->AfStillIsComplete(&af_status);
        i++;
        if (af_status)
        {
            LogDetail("==== still AF converge frame number %d", i);
            break;
        }
        gettimeofday(&currentTime,0);
    } while(calc_timediff(&stillafstarttime, &currentTime) < mStillAfMaxTimeMs);
    LogDetail("==== still Af status (1: success; 0: failed) = %d, time:%ld, Frames:%d\n",
        af_status, calc_timediff(&stillafstarttime, &currentTime), i);
    mAAA->AfStillStop ();
    mAAA->AeLock(false);

    return af_status;
}

status_t CameraHardware::sendCommand(int32_t command, int32_t arg1,
                                     int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardware::release()
{
    LogEntry(LOG_TAG, __FUNCTION__);

    if (mAeAfAwbThread != NULL) {
        mAeAfAwbThread->requestExit();
        mPreviewAeAfAwbRunning = true;
        mExitAeAfAwbThread = true;
        mPreviewAeAfAwbCondition.signal();
        mPreviewFrameCondition.signal();
        LogDetail("waiting 3A thread to exit:");
        mAeAfAwbThread->requestExitAndWait();
        mAeAfAwbThread.clear();
    }

    LogDetail("deleted the 3A thread:");
    if (mPreviewThread != NULL) {
        mPreviewThread->requestExit();
        mExitPreviewThread = true;
        mPreviewRunning = true; /* let it run so it can exit */
        mPreviewCondition.signal();
        mPreviewThread->requestExitAndWait();
        mPreviewThread.clear();
    }

    LogDetail("deleted the preview thread:");

    if (mAutoFocusThread != NULL) {
        mAutoFocusThread->requestExit();
        mExitAutoFocusThread = true;
        mAeAfAwbEndCondition.signal();
        mPreviewFrameCondition.signal();
        mAutoFocusThread->requestExitAndWait();
        mAutoFocusThread.clear();
    }
    LogDetail("deleted the autofocus thread:");

    if (mPictureThread != NULL) {
        mPictureThread->requestExitAndWait();
        mPictureThread.clear();
    }
    LogDetail("deleted the picture thread:");

    if (mCompressThread != NULL) {
        mCompressThread->requestExitAndWait();
        mCompressThread.clear();
    }
    LogDetail("BC, line:%d, deleted the compress thread:", __LINE__);

    if (mDvsThread != NULL) {
        mExitDvsThread = true;
        mDvsCondition.signal();
        mDvsThread->requestExitAndWait();
        mDvsThread.clear();
    }

    if(mAAA!=NULL)
        mAAA->Uninit();
    delete mAAA;
    mAAA=NULL;
    mCamera->deinitCamera();
    mCamera = NULL;
    LogDetail("dvs, line:%d, deleted the dvs thread:", __LINE__);
}

status_t CameraHardware::dump(int fd) const
{
    LogEntry(LOG_TAG, __FUNCTION__);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    cam_Window win_new, win_old;
    int ret;
    const char *new_value, *set_value;

        bool ae_to_manual = false;
        bool ae_to_aperture_priority = false;
        bool ae_to_shutter_priority = false;
        bool af_to_manual = false;

        // ae mode
        const char * pmode = CameraParameters::KEY_AE_MODE;
        new_value = p.get(pmode);
        if (!flush_only)
        {
            set_value = mParameters.get(pmode);
            LogDetail(" -ae mode = new \"%s\"  / current \"%s\"", new_value, set_value);
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

            LogDetail("     ++ Changed ae mode to %s, %d\n",p.get(pmode), ae_mode);
        }

        //Focus Mode
        const char * pfocusmode = CameraParameters::KEY_FOCUS_MODE;
        int focus_mode = p.getInt(pfocusmode);
        new_value = p.get(pfocusmode);
        if (!flush_only)
        {
            set_value =mParameters.get(pfocusmode);
            LogDetail(" - focus-mode = new \"%s\" (%d) / current \"%s\"", new_value, focus_mode, set_value);
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

            mAAA->AfSetMeteringMode(CAM_AF_METERING_MODE_SPOT);
            ret = mAAA->AfSetWindow(&win_new);
            LogDetail("AfSetWindow, tf, x_left:%d, y_top:%d, x_right:%d, y_bottom:%d, weight%d, result:%d",
                win_new.x_left, win_new.y_top, win_new.x_right, win_new.y_bottom, win_new.weight, ret);
            new_value = p.get(CameraParameters::KEY_FOCUS_MODE);
        } else {
            int mode, w, h;

            mAAA->AfGetMeteringMode(&mode);
            if (CAM_AF_METERING_MODE_SPOT == mode) {
                ret = mAAA->AfGetWindow(&win_old);
                LogDetail("AfGetWindow, x_left:%d, y_top:%d, x_right:%d, y_bottom:%d, weight%d, result:%d",
                    win_old.x_left, win_old.y_top, win_old.x_right, win_old.y_bottom, win_old.weight, ret);

                p.getPreviewSize(&w, &h);
                win_new.x_left = (w - 128) >> 1;    // 128 is the touch focus window's width
                win_new.y_top = (h - 96) >> 1;  // 96 is the touch focus window's height
                win_new.x_right = win_new.x_left + 128;
                win_new.y_bottom = win_new.y_top + 96;
                win_new.weight = win_old.weight;

                if (memcmp(&win_new, &win_old, sizeof(cam_Window))) {
                    ret = mAAA->AfSetWindow(&win_new);
                    LogDetail("AfSetWindow, x_left:%d, y_top:%d, x_right:%d, y_bottom:%d, weight%d, result:%d",
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

            mAAA->SetAfEnabled(true);
            mAAA->AfSetMode(afmode);

            LogDetail("     ++ Changed focus-mode to %s, afmode:%d",p.get(pfocusmode), afmode);
        }

        // white balance
        const char * pwb = CameraParameters::KEY_WHITE_BALANCE;
        int whitebalance = p.getInt(pwb);
        new_value = p.get(pwb);
        if (!flush_only)
        {
            set_value = mParameters.get(pwb);
            LogDetail(" - whitebalance = new \"%s\" (%d) / current \"%s\"", new_value,
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
                wb_mode = CAM_AWB_MODE_MANUAL_INPUT;
            else
                wb_mode = CAM_AWB_MODE_AUTO;

            if(wb_mode == CAM_AWB_MODE_MANUAL_INPUT)
                awb_to_manual = true;
            else
                awb_to_manual = false;
            mAAA->AwbSetMode(wb_mode);

            LogDetail("     ++ Changed whitebalance to %s, wb_mode:%d\n",p.get(pwb), wb_mode);
        }

        // ae metering mode
        const char * paemeteringmode = CameraParameters::KEY_AE_METERING_MODE;
        new_value = p.get(paemeteringmode);
        if (!flush_only)
        {
            set_value = mParameters.get(paemeteringmode);
            LogDetail(" -ae metering mode = new \"%s\"  / current \"%s\"", new_value, set_value);
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
            else if(!strcmp(new_value, "customized")) {
                ae_metering_mode = CAM_AE_METERING_MODE_CUSTOMIZED;
                mAAA->AeSetMeteringWeightMap(&mAeWeightMap);
            }
            else
                ae_metering_mode = CAM_AE_METERING_MODE_AUTO;
            mAAA->AeSetMeteringMode(ae_metering_mode);

            LogDetail("     ++ Changed ae metering mode to %s, %d\n",p.get(paemeteringmode), ae_metering_mode);
        }

        // af metering mode
        const char * pafmode = CameraParameters::KEY_AF_METERING_MODE;
        new_value = p.get(pafmode);
        if (!flush_only)
        {
            set_value = mParameters.get(pafmode);
            LogDetail(" -af metering mode = new \"%s\"  / current \"%s\"", new_value, set_value);
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

            LogDetail("     ++ Changed af metering mode to %s, %d\n",p.get(pafmode), af_metering_mode);
        }

        // ae lock mode
        const char * paelock = CameraParameters::KEY_AE_LOCK_MODE;
        new_value = p.get(paelock);
        if(!flush_only)
        {
            set_value = mParameters.get(paelock);
            LogDetail(" -ae lock mode = new \"%s\"  / current \"%s\"", new_value, set_value);
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

            LogDetail("     ++ Changed ae lock mode to %s, %d\n",p.get(paelock), ae_lock);
        }

         // backlight correction
        const char * pbkcor = CameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE;
        new_value = p.get(pbkcor);
        if (!flush_only)
        {
            set_value = mParameters.get(pbkcor);
            LogDetail(" -ae backlight correction = new \"%s\"  / current \"%s\"", new_value, set_value);
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

            LogDetail("     ++ Changed ae backlight correction to %s, %d\n",p.get(pbkcor), backlight_correction);
        }

        // redeye correction
        const char * predeye = CameraParameters::KEY_RED_EYE_MODE;
        new_value = p.get(predeye);
        if (!flush_only)
        {
            set_value = mParameters.get(predeye);
            LogDetail(" -red eye correction = new \"%s\"  / current \"%s\"", new_value, set_value);
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

            LogDetail("     ++ Changed red eye correction to %s, %d\n",p.get(predeye), red_eye_correction);
        }

        // awb mapping mode
        const char * pawbmap = CameraParameters::KEY_AWB_MAPPING_MODE;
        new_value = p.get(pawbmap);
        if (!flush_only)
        {
            set_value = mParameters.get(pawbmap);
            LogDetail(" -awb mapping = new \"%s\"  / current \"%s\"", new_value, set_value);
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
            else if(!strcmp(new_value, "auto"))
                awb_mapping = CAM_AWB_MAP_AUTO;
            else
                awb_mapping = CAM_AWB_MAP_AUTO;

            mAAA->AwbSetMapping(awb_mapping);

            LogDetail("     ++ Changed awb mapping to %s, %d\n",p.get(pawbmap), awb_mapping);
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
                LogDetail(" -color temperature = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || awb_to_manual == true) {
                int ct;

                ct = atoi(new_value);
                mAAA->AwbSetManualColorTemperature(ct, true);

                LogDetail("     ++ Changed color temperature to %s, %d\n",p.get(pct), ct);
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
                LogDetail(" -focus position = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || af_to_manual == true) {
                float focus_pos;

                focus_pos = atof(new_value);
                mAAA->AfSetMode(CAM_AF_MODE_MANUAL);
                mManualFocusPosi = (int)(100.0 * focus_pos);

                LogDetail("     ++ Changed focus position to %s, %f\n",p.get(pfocuspos), focus_pos);
            }
        }
        else if (cur_af_mode == CAM_AF_MODE_INFINITY)
        {
            mManualFocusPosi = 500;         // 500cm as infinity position
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
                LogDetail(" -manual shutter = new \"%s\"  / current \"%s\"", new_value, set_value);
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
                    mAAA->AeSetManualShutter(shutter, true);
                    LogDetail("     ++ Changed shutter to %s, %f\n",p.get(pshutter), shutter);
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
                LogDetail(" -manual iso = new \"%s\"  / current \"%s\"", new_value, set_value);
            }
            else
            {
                set_value = new_value;
            }
            if (strcmp(set_value, new_value) != 0 || flush_only || ae_to_manual == true) {
                float iso;

                iso = atoi(new_value + 4);
                mAAA->AeSetManualIso(iso, true);

                LogDetail("     ++ Changed manual iso to %s, %f\n",p.get(piso), iso);
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
            LogDetail(" EV Index  = new \"%s\" (%d) / current \"%s\"",new_value, exposure, set_value);
        }
        else
        {
            set_value = new_value;
        }
        if (strcmp(set_value, new_value) != 0 || flush_only) {
            mAAA->AeSetEv(atoi(new_value) * comp_step);
            float ev = 0;
            mAAA->AeGetEv(&ev);
            LogDetail("      ++Changed exposure effect to index %s, ev valule %f",p.get(pexp), ev);
        }

        //Flicker Mode
        const char * pantibanding = CameraParameters::KEY_ANTIBANDING;
        int antibanding = p.getInt(pantibanding);
        new_value = p.get(pantibanding);
        if (!flush_only)
        {
            set_value = mParameters.get(pantibanding);
            LogDetail(" - antibanding = new \"%s\" (%d) / current \"%s\"", new_value, antibanding, set_value);
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

            LogDetail("     ++ Changed antibanding to %s, antibanding val:%d",p.get(pantibanding), bandingval);
        }

        // Scene Mode
        const char* pscenemode = CameraParameters::KEY_SCENE_MODE;
        int scene_mode = p.getInt(pscenemode);
        new_value = p.get(pscenemode);
        if (!flush_only)
        {
            set_value = mParameters.get(pscenemode);
            LogDetail(" - scene-mode = new \"%s\" (%d) / current \"%s\"", new_value,
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
                LogDetail("     ++ Not supported scene-mode");
            }

            if (scene_mode != CAM_AE_SCENE_MODE_AUTO) {
                p.set(CameraParameters::KEY_FOCUS_MODE, "auto");
                p.set(CameraParameters::KEY_WHITE_BALANCE, "auto");
            }

            mAAA->AeSetSceneMode (scene_mode);
        }

        //flash mode
        int flash_mode = p.getInt("flash-mode");
        new_value = p.get("flash-mode");
        if (!flush_only)
        {
            set_value = mParameters.get("flash-mode");
            LogDetail(" - flash-mode = new \"%s\" (%d) / current \"%s\"", new_value, flash_mode, set_value);
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
                LogDetail("     ++ Not supported flash-mode");
            }
            mCamera->setFlashMode(flash_mode);
            mAAA->AeSetFlashMode (flash_mode);
        }

    mFlush3A = false;

    return 0;
}

status_t CameraHardware::setParameters(const char* params)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    CameraParameters p;

    String8 str_params(params);
    p.unflatten(str_params);

    return setParameters(p);
}

status_t CameraHardware::setParameters(const CameraParameters& params)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int ret = NO_ERROR;
    Mutex::Autolock lock(mLock);
    // XXX verify params
    params.dump();  // print parameters for debug

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
        LogDetail("only yuv420sp, yuv422i-yuyv, rgb565 preview are supported, use rgb565");
        new_preview_format = V4L2_PIX_FMT_RGB565;
    }

    if (0 < new_preview_width && 0 < new_preview_height && new_value != NULL) {
        LogDetail(" - Preview pixel format = new \"%s\"  / current \"%s\"",
             new_value, set_value);

        if (mCamera->setPreviewSize(new_preview_width, new_preview_height,
                                    new_preview_format) < 0) {
            LogError("Fail on setPreviewSize(width(%d), height(%d), format(%d))",
                     new_preview_width, new_preview_height, new_preview_format);
        } else {
            p.setPreviewSize(new_preview_width, new_preview_height);
            p.setPreviewFormat(new_value);
            LogDetail("     ++ Changed Preview Pixel Format to %s",p.getPreviewFormat());
        }
    }

    // preview frame rate
    int new_fps = p.getPreviewFrameRate();
    int set_fps = mParameters.getPreviewFrameRate();
    LogDetail(" - FPS = new \"%d\" / current \"%d\"",new_fps, set_fps);
    if (new_fps != set_fps) {
        p.setPreviewFrameRate(new_fps);
        LogDetail("     ++ Changed FPS to %d",p.getPreviewFrameRate());
    }
    LogDetail("PREVIEW SIZE: %dx%d, FPS: %d", new_preview_width, new_preview_height,
         new_fps);

    //Picture format
    int new_picture_width, new_picture_height;
    const char *new_format = p.getPictureFormat();
    if (strcmp(new_format, "jpeg") == 0)
        mPicturePixelFormat = mHwJpegBufferShareEn ? V4L2_PIX_FMT_NV12 : V4L2_PIX_FMT_YUV420;
    else {
        LogDetail("Only jpeg still pictures are supported, new_format:%s", new_format);
    }

    LogDetail(" - Picture pixel format = new \"%s\"", new_format);
    p.getPictureSize(&new_picture_width, &new_picture_height);

    // RAW picture data format
    if (mSensorType == SENSOR_TYPE_RAW) {
        const char *raw_format = p.get(CameraParameters::KEY_RAW_DATA_FORMAT);
        LogDetail("raw format is %s", raw_format);
        // FIXME: only support bayer dump now
        if (strcmp(raw_format, "bayer") == 0) {
            mCamera->setRawFormat(RAW_BAYER);
        } else if (strcmp(raw_format, "yuv") == 0) {
        mCamera->setRawFormat(RAW_YUV);
        } else {
            mCamera->setRawFormat(RAW_NONE);
        }
    } else
        mCamera->setRawFormat(RAW_NONE);

    // burst capture
    mBCNumReq = p.getInt(CameraParameters::KEY_BURST_LENGTH);
    mBCEn = (mBCNumReq > 1) ? true : false;
    if (mBCEn) {
        mBCNumSkipReq = p.getInt(CameraParameters::KEY_BURST_SKIP_FRAMES);
    } else {
        mBCNumReq = 1;
        mBCNumSkipReq = 0;
    }
    LogDetail("BC, line:%d,burst len, en:%d, reqnum:%d, skipnum:%d", __LINE__, mBCEn, mBCNumReq, mBCNumSkipReq);

    if (mHwJpegBufferShareEn) {
        /*there is limitation for picture resolution with hwlibjpeg buffer share
        if picture resolution below 640*480 , we have to set mPicturePixelFormat
        back to YUV420 and go through software encode path*/
        if(new_picture_width <= 640 || new_picture_height <=480)
            mPicturePixelFormat = V4L2_PIX_FMT_YUV420;
        else
            mPicturePixelFormat = V4L2_PIX_FMT_NV12;
    }

    LogDetail("new_picture_width %d new_picture_height = %d",
         new_picture_width, new_picture_height);

    if (0 < new_picture_width && 0 < new_picture_height) {
        if (mCamera->setSnapshotSize(new_picture_width, new_picture_height,
                                     mPicturePixelFormat) < 0) {
            LogError("Fail on mCamera->setSnapshotSize(width(%d), height(%d))",
                 new_picture_width, new_picture_height);
            ret = UNKNOWN_ERROR;
        } else {
            p.setPictureSize(new_picture_width, new_picture_height);
            p.setPictureFormat(new_value);
        }
    }

    //thumbnail
    int new_thumbnail_w,new_thumbnail_h;
    new_thumbnail_w = p.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    new_thumbnail_h = p.getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    LogDetail("thumbnail size change :new wx: %d x %d",new_thumbnail_w,new_thumbnail_h);
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH, new_thumbnail_w);
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT,new_thumbnail_h);
    //Video recording
    //int vfmode = p.getInt("camera-mode");
    int vfmode = 2;
	LogDetail("vfmode %d", vfmode);
    int mVideoFormat = V4L2_PIX_FMT_NV12;
    //Deternmine the current viewfinder MODE.
    if (vfmode == 1) {
        LogDetail("Entering the video recorder mode");
        Mutex::Autolock lock(mRecordLock);
        mVideoPreviewEnabled = true; //viewfinder running in preview mode
    } else if (vfmode == 2) {
        LogDetail("Entering the normal preview mode");
        Mutex::Autolock lock(mRecordLock);
        mVideoPreviewEnabled = false; //viewfinder running in video mode
    } else {
        LogDetail("Entering the cts preview mode");
        Mutex::Autolock lock(mRecordLock);
        mVideoPreviewEnabled = true; //viewfinder running in video mode
    }
    //Zoom is a invalid value or not
    int zoom = p.getInt(CameraParameters::KEY_ZOOM);
    if(zoom > MAX_ZOOM_LEVEL || zoom < MIN_ZOOM_LEVEL)
        return BAD_VALUE;

    // preview fps range is a invalid value range or not
    int min_fps,max_fps;
    p.getPreviewFpsRange(&min_fps, &max_fps);
    if(min_fps == max_fps || min_fps > max_fps)
        return BAD_VALUE;

    // zoom is not supported in video mode for soc sensor.
    if (vfmode != 2 && mSensorType == SENSOR_TYPE_SOC)
	p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "false");
    else
	p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");

    int pre_width, pre_height, pre_size, pre_padded_size, rec_w, rec_h;
    mCamera->getPreviewSize(&pre_width, &pre_height, &pre_size, &pre_padded_size);
    p.getVideoSize(&rec_w, &rec_h);

    if(checkRecording(rec_w, rec_h)) {
        LogDetail("line:%d, before setRecorderSize. w:%d, h:%d, format:%d", __LINE__, rec_w, rec_h, mVideoFormat);
        mCamera->setRecorderSize(rec_w, rec_h, mVideoFormat);
    }
    else {
        LogDetail("line:%d, before setRecorderSize. w:%d, h:%d, format:%d", __LINE__, pre_width, pre_height, mVideoFormat);
        mCamera->setRecorderSize(pre_width, pre_height, mVideoFormat);
    }

    // update 3A parameters to mParameters and 3A inside
    if (mSensorType == SENSOR_TYPE_RAW)
        update3AParameters(p, mFlush3A);

    setISPParameters(p,mParameters);

    //Update the parameters
    mParameters = p;
    return ret;
}
/*  this function will compare 2 parameters.
 *  parameters will be set to isp if
 *  new and current parameter diffs.
 */
int CameraHardware::setISPParameters(
                const CameraParameters &new_params,
                    const CameraParameters &old_params)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    const char *new_value, *set_value;
    int ret,ret2;
    static int effect = old_params.getInt(CameraParameters::KEY_EFFECT);

    ret = ret2 = -1;

    //process zoom
    int zoom = new_params.getInt(CameraParameters::KEY_ZOOM);
    mCamera->set_zoom_val(zoom);
    if (mSensorType == SENSOR_TYPE_RAW) {
        // Color Effect
        new_value = new_params.get(CameraParameters::KEY_EFFECT);
        set_value = old_params.get(CameraParameters::KEY_EFFECT);
        LogDetail(" - effect = new \"%s\" (%d) / current \"%s\"",new_value, effect, set_value);
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
                LogDetail("Changed effect to %s", new_params.get(CameraParameters::KEY_EFFECT));
            }
        }

        // xnr
        int xnr = old_params.getInt(CameraParameters::KEY_XNR);
        new_value = new_params.get(CameraParameters::KEY_XNR);
        set_value = old_params.get(CameraParameters::KEY_XNR);
        LogDetail(" - xnr = new \"%s\" (%d) / current \"%s\"",new_value, xnr, set_value);
        if (strcmp(set_value, new_value) != 0) {
            if (!strcmp(new_value, "false"))
                ret = mCamera->setXNR(false);
            else if (!strcmp(new_value, "true"))
                ret = mCamera->setXNR(true);
            if (!ret) {
                LogDetail("Changed xnr to %s", new_params.get(CameraParameters::KEY_XNR));
            }
        }
        // gdc/cac
        int gdc = old_params.getInt(CameraParameters::KEY_GDC);
        new_value = new_params.get(CameraParameters::KEY_GDC);
        set_value = old_params.get(CameraParameters::KEY_GDC);
        LogDetail(" - gdc = new \"%s\" (%d) / current \"%s\"",new_value, gdc, set_value);
        if (strcmp(set_value, new_value) != 0) {
            if (!strcmp(new_value, "false"))
                ret = mCamera->setGDC(false);
            else if (!strcmp(new_value, "true"))
                ret = mCamera->setGDC(true);
            if (!ret) {
                LogDetail("Changed gdc to %s", new_params.get(CameraParameters::KEY_GDC));
            }
        }

        // DVS
        int dvs = old_params.getInt(CameraParameters::KEY_DVS);
        new_value = new_params.get(CameraParameters::KEY_DVS);
        set_value = old_params.get(CameraParameters::KEY_DVS);
        LogDetail(" - dvs = new \"%s\" (%d) / current \"%s\"",new_value, dvs, set_value);
        if (strcmp(set_value, new_value) != 0) {
            if (!strcmp(new_value, "false")) {
                ret = mCamera->setDVS(false);
            }
            else if (!strcmp(new_value, "true")) {
                ret = mCamera->setDVS(true);
            }
            if (!ret) {
                LogDetail("Changed dvs to %s", new_params.get(CameraParameters::KEY_DVS));
            }

            // in the video mode and preview is running status
            if (mVideoPreviewEnabled && mPreviewRunning) {
                LogDetail("dvs,line:%d, resetCamera", __LINE__);
                //resetCamera could let the DVS setting valid. dvs set must before fmt setting
                mCamera->resetCamera(); // the dvs setting will be enabled in the configuration stage
                if (mCamera->getDVS()) {
                    LogDetail("dvs,line:%d, signal thread", __LINE__);
                    mDvsCondition.signal();
                }
            }
        }

        // tnr
        int tnr = old_params.getInt(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION);
        new_value = new_params.get(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION);
        set_value = old_params.get(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION);
        LogDetail(" - temporal-noise-reduction = new \"%s\" (%d) / current \"%s\"",new_value, tnr, set_value);
        if (strcmp(set_value, new_value) != 0) {
            if (!strcmp(new_value, "on"))
                ret = mCamera->setTNR(true);
            else if (!strcmp(new_value, "off"))
                ret = mCamera->setTNR(false);
            if (!ret) {
                LogDetail("Changed temporal-noise-reduction to %s",
                        new_params.get(CameraParameters::KEY_TEMPORAL_NOISE_REDUCTION));
            }
        }

#ifdef TUNING_EDGE_ENHACNMENT
        // nr and ee
        int nr_ee = old_params.getInt(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
        new_value = new_params.get(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
        set_value = old_params.get(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
        LogDetail(" -  noise-reduction-and-edge-enhancement= new \"%s\" (%d) / current \"%s\"",new_value, nr_ee, set_value);
        if (strcmp(set_value, new_value) != 0) {
            if (!strcmp(new_value, "on")) {
                ret = mCamera->setNREE(true);
            }
            else if (!strcmp(new_value, "off")) {
                ret = mCamera->setNREE(false);
            }
            if (!ret) {
                LogDetail("Changed  noise-reduction-and-edge-enhancement to %s",
                        new_params.get(CameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT));
            }
        }
#endif

        //macc
        int color = 0;
        int macc = old_params.getInt(CameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
        new_value = new_params.get(CameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
        set_value = old_params.get(CameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
        LogDetail(" - multi-access-color-correction = new \"%s\" (%d) / current \"%s\"",new_value, macc, set_value);
        if (strcmp(set_value, new_value) != 0) {
            if (!strcmp("enhance-none", new_value))
                color = effect;
            else if (!strcmp("enhance-sky", new_value))
                color = V4L2_COLORFX_SKY_BLUE;
            else if (!strcmp("enhance-grass", new_value))
                color = V4L2_COLORFX_GRASS_GREEN;
            else if (!strcmp("enhance-skin", new_value))
                color = V4L2_COLORFX_SKIN_WHITEN;
            ret = mCamera->setMACC(color);

            if (!ret) {
                LogDetail("Changed multi-access-color-correction to %s",
                        new_params.get("multi-access-color-correction"));
            }
        }
    }

    return 0;
}

char* CameraHardware::getParameters() const
{
    LogEntry(LOG_TAG, __FUNCTION__);
    char* ret;
    Mutex::Autolock lock(mLock);
    String8 params = mParameters.flatten();
    ret = strdup(params.string());
    return ret;
}

void CameraHardware::putParameters(char *params)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (params != NULL)
        free(params);
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
    LogEntry(LOG_TAG, __FUNCTION__);
    status_t ret = -1;

    if (!use_file_input) {
        LogError("File input mode is disabled");
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
    LogEntry(LOG_TAG, __FUNCTION__);
    if (mBCEn) {
        mFlashNecessary = false;
        return 0;
    } else
        return mAAA->AeIsFlashNecessary (&mFlashNecessary);
}
/* The pre-flash sequence consists of 3 preview frames.
 * For each frame the 3A library will run in a specific mode.
 */
void CameraHardware::runPreFlashSequence(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int index, cnt = 0;
    void *data;
    enum atomisp_frame_status status;
    int cur_ae_mode;

    mAAA->AeGetMode (&cur_ae_mode);
    if (cur_ae_mode == CAM_AE_MODE_MANUAL)
        mAAA->SetAeEnabled(false);
    else
        mAAA->SetAeEnabled(true);
    mAAA->SetAwbEnabled(true);

    // Stage 1
    index = mCamera->getPreview(&data, &status);
    if (index < 0)
        goto error;
    mCamera->putPreview(index);
    // TODO: need to fix this!
    //mAAA->PreFlashProcess(CAM_FLASH_STAGE_NONE);

    // Skip 1 frame to get exposure from Stage 1
    index = mCamera->getPreview(&data, &status);
    if (index < 0)
        goto error;
    mCamera->putPreview(index);

    // Stage 2
    index = mCamera->getPreview(&data, &status);
    if (index < 0)
        goto error;
    mCamera->putPreview(index);
    // TODO: need to fix this!
    //mAAA->PreFlashProcess(CAM_FLASH_STAGE_PRE);

    // Skip 1 frame to get exposure from Stage 2
    index = mCamera->getPreview(&data, &status);
    if (index < 0)
        goto error;
    mCamera->putPreview(index);

    // Stage 3: get the flash-exposed preview frame
    // and let the 3A library calculate the exposure
    // settings for the flash-exposed still capture.
    // We check the frame status to make sure we use
    // the flash-exposed frame.
    mPreFlashSucceeded = mCamera->requestFlash(1);

    while (1) {
        index = mCamera->getPreview(&data, &status);
        if (index < 0)
            goto error;
        mCamera->putPreview(index);
        if (!mPreFlashSucceeded)
            break;
        if (status == ATOMISP_FRAME_STATUS_FLASH_EXPOSED ||
            status == ATOMISP_FRAME_STATUS_FLASH_FAILED)
            break;
        /* safety precaution */
        if (cnt++ == FLASH_FRAME_TIMEOUT) {
            LogError("terminating pre-flash loop, no flashed frame received");
            mPreFlashSucceeded = false;
            break;
        }
    }
    /* TODO: need to fix this!
    if (mPreFlashSucceeded && status == ATOMISP_FRAME_STATUS_FLASH_EXPOSED)
        mAAA->PreFlashProcess(CAM_FLASH_STAGE_MAIN);
    else
        mAAA->AeAfAwbProcess(true);
     */
error:
    mAAA->SetAeEnabled(false);
    mAAA->SetAwbEnabled(false);
}

//3A processing
void CameraHardware::update3Aresults(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    mAAA->SetAeEnabled (true);
    mAAA->AeLock(true);
    // TODO: need to fix this!
    //mAAA->AeAfAwbProcess (false);
    mAAA->AeLock(false);
    mAAA->SetAeEnabled (false);
}

int CameraHardware::SnapshotPostProcessing(void *img_data, int width, int height)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    // do red eye removal
    int img_size;

    // FIXME:
    // currently, if capture resolution more than 5M, camera will hang if
    // ShRedEye_Remove() is called in 3A library
    // to workaround and make system not crash, maximum resolution for red eye
    // removal is restricted to be 5M
    if (width > 2560 || height > 1920 || awb_to_manual)
    {
        LogDetail(" Bug here: picture size must not more than 5M for red eye removal");
        return -1;
    }

    img_size = mCamera->m_frameSize (mPicturePixelFormat, width, height);

    mAAA->DoRedeyeRemoval (img_data, img_size, width, height, mPicturePixelFormat);

    return 0;
}

void CameraHardware::setFlip(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if(mCameraId == CAMERA_FACING_FRONT) {
        int rotation = mParameters.getInt(CameraParameters::KEY_ROTATION);
        if(rotation == 270 || rotation == 90)
            mFlipMode = FLIP_V;
        else
            mFlipMode = FLIP_H;

        mCanFlip = true;
        mCamera->setSnapshotFlip(true,mFlipMode);
    }
}

void CameraHardware::resetFlip(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if(mCanFlip)
        mCamera->setSnapshotFlip(false,mFlipMode);
}

void CameraHardware::setupPlatformType(void)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    int i, j;
    for (i = 0; i < MAX_CAMERAS; i++) {
        //Remove the blank and i2c name
        for (j = 0; j < MAX_SENSOR_NAME_LENGTH; j ++) {
            if (camInfo[i].name[j] == ' ') {
                camInfo[i].name[j] = '\0';
                break;
            }
        }
        LogDetail("Detected sensor %s\n", camInfo[i].name);

        if (!strncmp(camInfo[i].name, CDK_PRIMARY_SENSOR_NAME,
            sizeof(camInfo[i].name))) {
            mPreviewSkipFrame = 4;
            mSnapshotSkipFrame = 1;
        } else if (!strncmp(camInfo[i].name, CDK_SECOND_SENSOR_NAME,
            sizeof(camInfo[i].name))) {
            mPreviewSkipFrame = 4;
            mSnapshotSkipFrame = 1;
        } else if (!strncmp(camInfo[i].name, PR2_PRIMARY_SENSOR_NAME,
            sizeof(camInfo[i].name))) {
            mPreviewSkipFrame = 1;
            mSnapshotSkipFrame = 2;
        } else if (!strncmp(camInfo[i].name, PR2_SECOND_SENSOR_NAME,
            sizeof(camInfo[i].name))) {
            mPreviewSkipFrame = 1;
            mSnapshotSkipFrame = 2;
        } else {
            mPreviewSkipFrame = 1;
            mSnapshotSkipFrame = 2;
        }
    }
}

status_t CameraHardware::storeMetaDataInBuffers(bool enable)
{
    return NO_ERROR;
}

/* This function will be called when the camera service is created.
 * Do some init work in this function.
 */
int CameraHardware::getNumberOfCameras()
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (num_cameras != 0)
        return num_cameras;
    int ret;
    struct v4l2_input input;
    int fd = -1;
    char *dev_name = "/dev/video0";

    fd = open(dev_name, O_RDWR);
    if (fd <= 0) {
        LogError("Error opening video device %s: %s",
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
        camInfo[i].port = input.reserved[1];
        strncpy(camInfo[i].name, (const char *)input.name, MAX_SENSOR_NAME_LENGTH);
    }

    close(fd);

    num_cameras = i;

    return num_cameras;
}

CameraHardware* CameraHardware::singleton;

CameraHardware* CameraHardware::createInstance(int cameraId)
{
    if (singleton != NULL) {
        return singleton;
    }
    singleton = new CameraHardware(cameraId);
    return singleton;
}


int CameraHardware::getCameraInfo(int cameraId, struct camera_info* cameraInfo)
{
    LogEntry(LOG_TAG, __FUNCTION__);
    if (cameraId >= MAX_CAMERAS)
        return -EINVAL;
    memcpy(cameraInfo, &HAL_cameraInfo[cameraId], sizeof(camera_info));
    return 0;
}

}; // namespace android
