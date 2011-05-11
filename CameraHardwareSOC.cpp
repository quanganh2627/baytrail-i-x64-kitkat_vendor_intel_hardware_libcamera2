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

#define LOG_TAG "CameraHardwareSOC"
#include <utils/Log.h>

#include "CameraHardwareSOC.h"
#include <utils/threads.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "SkBitmap.h"
#include "SkImageEncoder.h"
#include "SkStream.h"

namespace android {

#define   MEMORY_MAP
//#undef  MEMORY_MAP


#define	CAMLOGD(fmt, arg...)    LOGD("%s(line %d): " fmt, \
            __FUNCTION__, __LINE__, ##arg)

bool soc_share_buffer_caps_set=false;
extern struct parameters *sensors[];

CameraHardwareSOC::CameraHardwareSOC(int cameraId)
                  : mParameters(),
		    mCameraId(cameraId),
		    mPreviewFrame(0),
		    mPostPreviewFrame(0),
		    mRecordingFrame(0),
		    mPostRecordingFrame(0),
		    mCamera(0),
		    mSensorNow(NULL),
		    mBlockNumber(0),
		    mRecordingRunning(false),
                    mPreviewFrameSize(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    mPreviewLastTS(0),
                    mPreviewLastFPS(0),
                    mRecordingLastTS(0),
                    mRecordingLastFPS(0)
{
    mCamera = new IntelCameraSOC(mCameraId);
    initDefaultParameters();
    mCameraState = CAM_DEFAULT;
    LOGD("libcamera version: 2011-03-01 1.0.1");
}

void CameraHardwareSOC::initHeapLocked(int size)
{
    LOGD("xiaolin@%s(), size %d, preview size%d", __func__, size, mPreviewFrameSize);
    int recordersize;
    if (size != mPreviewFrameSize) {
        const char *preview_fmt;
        unsigned int page_size = getpagesize();
        unsigned int size_aligned = (size + page_size - 1) & ~(page_size - 1);
        preview_fmt = mParameters.getPreviewFormat();

        if (strcmp(preview_fmt, "yuv420sp") == 0) {
            recordersize = size;
        }  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0) {
            recordersize = size;
        } else if (strcmp(preview_fmt, "rgb565") == 0) {
            recordersize = (size * 3)/4;
        } else {
            LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
            recordersize = size;
        }

#if ENABLE_BUFFER_SHARE_MODE
        recordersize = sizeof(unsigned int*);
#endif
        mPreviewBuffer.heap = new MemoryHeapBase(size_aligned * kBufferCount);
        mRecordingBuffer.heap = new MemoryHeapBase(recordersize * kBufferCount);

        for (int i=0; i < kBufferCount; i++) {
            mPreviewBuffer.flags[i] = 0;
            mRecordingBuffer.flags[i] = 0;

            mPreviewBuffer.base[i] =
                new MemoryBase(mPreviewBuffer.heap, i * size_aligned, size_aligned);
            clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
            mPreviewBuffer.start[i] = (uint8_t *)mPreviewBuffer.heap->base() + (i * size_aligned);

            mRecordingBuffer.base[i] =
                new MemoryBase(mRecordingBuffer.heap, i *recordersize, recordersize);
            clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
            mRecordingBuffer.start[i] = (uint8_t *)mRecordingBuffer.heap->base() + (i * recordersize);

#if ENABLE_BUFFER_SHARE_MODE
            memset((char*)mRecordingBuffer.start[i], 0, recordersize);
            mRecordingBuffer.pointerArray[i] = NULL;
#endif


        }
        LOGD("%s Re Alloc previewframe size=%d, recordersiz=%d",__func__, size, recordersize);
        mPreviewFrameSize = size;
    }

}

void CameraHardwareSOC::initPreviewBuffer() {

}

void CameraHardwareSOC::deInitPreviewBuffer() {

}

void CameraHardwareSOC::initRecordingBuffer() {

}

void CameraHardwareSOC::deInitRecordingBuffer() {

}

void CameraHardwareSOC::initDefaultParameters()
{
    CameraParameters p;
    struct parameters **sensorNow = NULL;
   
    /* init parameters supportted for App */
    char sensorID[32]; 
    if(0 == mCamera->getSensorID(sensorID))
    {
        LOGD("%s:: get sensor id is %s", __FUNCTION__, sensorID);
	
        sensorNow = sensors;
        while(NULL != *sensorNow){
            if( strstr(sensorID, (*sensorNow)->sensorID) ){
                break;
            }
            ++sensorNow;
        }
        initCameraParameters(p, sensorNow);
    }
    else
    {
        LOGE("%s::get sensor id failed", __FUNCTION__);
        //TODO: Use default paramera set here?
    }
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
    p.setPreviewFormat("yuv420sp");
#else
    p.setPreviewFormat("rgb565");
#endif

    mParameters = p;
    mSensorNow = sensorNow;

}

int CameraHardwareSOC::initCameraParameters(CameraParameters &p, struct parameters **sensorNow){
    /* init framerate */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, 
		    CameraParameters::KEY_PREVIEW_FRAME_RATE, (*sensorNow)->framerate_map, false);
    /* init videoformat */
    setCameraParameters(p, CameraParameters::KEY_VIDEO_FRAME_FORMAT, NULL, 
		    (*sensorNow)->videoformat_map, false);
    /* init previewformat */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, 
		    CameraParameters::KEY_PREVIEW_FORMAT, (*sensorNow)->previewformat_map, false);
    /* init previewsize */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, 
		    CameraParameters::KEY_PREVIEW_SIZE, (*sensorNow)->previewsize_map, false);
    /* init pictureformat */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, 
		    CameraParameters::KEY_PICTURE_FORMAT, (*sensorNow)->pictureformat_map, false);
    /* init picturesize */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
		    CameraParameters::KEY_PICTURE_SIZE, (*sensorNow)->picturesize_map, false);

    /* init jpegquality*/
    setCameraParameters(p, KEY_SUPPORTED_JPEG_QUALITY, 
		    CameraParameters::KEY_JPEG_QUALITY, (*sensorNow)->jpegquality_map, true);
    /* init rotation */
    //setCameraParameters(p, KEY_SUPPORTED_ROTATIONS, 
		    //CameraParameters::KEY_ROTATION, (*sensorNow)->rotation_map, true);


    /* init flash-mode */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_FLASH_MODES, 
		    CameraParameters::KEY_FLASH_MODE, (*sensorNow)->flashmode_map, true);
    /* init focus-mode */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_FOCUS_MODES, 
		    CameraParameters::KEY_FOCUS_MODE, (*sensorNow)->focusmode_map, true);
    /* init ColorEffect */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_EFFECTS, 
		    CameraParameters::KEY_EFFECT, (*sensorNow)->effect_map, true);
    /* init WhiteBalance */
    setCameraParameters(p, CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, 
		    CameraParameters::KEY_WHITE_BALANCE, (*sensorNow)->wb_map, true);
    /* init EVoffset(Exposure) */
    setCameraParameters(p, NULL, NULL, (*sensorNow)->exposure_map, true);

    return 0;
}

void CameraHardwareSOC::setCameraParameters(CameraParameters &p, const char *key_supported, 
		const char *key, const struct setting_map *map, bool flag)
{
    int ret;
    const int strlen = 256;
    char strbuf[strlen+1];
    char *pbuf;
    int n, pos, first;

    n = 0; pos = 0; first = 0;
    strbuf[0] = 0;
    pbuf = strbuf;

    const struct setting_map * trav = map;
    if(map){
        while (trav->key) {
            if(first){
                pos += snprintf(pbuf+pos, strlen-pos, ",");
                if(pos>=strlen-1)
                    break;
            }
            else{
                first = 1;
                //set default to key
                if(key){
                    p.set(key, trav->key);
                    CAMLOGD("set default \"%s\" to \"%s\"", key, trav->key);
                }
            }

            pos += snprintf(pbuf+pos, strlen-pos, "%s", trav->key);
            CAMLOGD("KEY:%s, VALUE:%d\n", trav->key, trav->value);
            if(flag)
                p.set(trav->key, trav->value); //set value to subkey
            if(pos>=strlen-1)
                break;
            n++;
            trav++;
        }
    
        if(key_supported){
            if (n>0){
                CAMLOGD("set %s=%s\n", key_supported, strbuf);
                p.set(key_supported, strbuf); //set parameters supported
            }
        }
    }

    return;
}

CameraHardwareSOC::~CameraHardwareSOC()
{
    delete mCamera;
    singleton.clear();
}

sp<IMemoryHeap> CameraHardwareSOC::getPreviewHeap() const
{
    return mPreviewBuffer.heap;
}

sp<IMemoryHeap> CameraHardwareSOC::getRawHeap() const
{
    return mRawHeap;
}

void CameraHardwareSOC::setCallbacks(notify_callback notify_cb,
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

void CameraHardwareSOC::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void CameraHardwareSOC::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool CameraHardwareSOC::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------
int CameraHardwareSOC::previewThread()
{
    if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
        // Get a preview frame
        int previewFrame = mPreviewFrame;
	if( !isBFSet(mPreviewBuffer.flags[previewFrame], BF_ENABLED)
	    && !isBFSet(mPreviewBuffer.flags[previewFrame], BF_LOCKED)) {

	    setBF(&mPreviewBuffer.flags[previewFrame],BF_LOCKED);
	    mCamera->captureGrabFrame(); 
		
		const char *preview_fmt;
		preview_fmt = mParameters.getPreviewFormat();
		
		if (!strcmp(preview_fmt, "yuv420sp") ||
		    !strcmp(preview_fmt, "yuv422i-yuyv") ||
		    !strcmp(preview_fmt, "rgb565")) {
#ifdef BOARD_USE_CAMERA_TEXTURE_STREAMING
			/* only copy current frame id */
			unsigned int frame_id = mCamera->captureGetFrameID();
			memcpy(mPreviewBuffer.start[previewFrame],
			       &frame_id,
			       sizeof(unsigned int));
			CAMLOGD("frame_id = %d\n", frame_id);
#else
			mCamera->captureGetFrame(mPreviewBuffer.start[previewFrame]);
#endif
		} else {
		  LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
		  return -1;
		}
		//LOGD("xiaolin@%s()222@", __func__);
	    clrBF(&mPreviewBuffer.flags[previewFrame],BF_LOCKED);
	    setBF(&mPreviewBuffer.flags[previewFrame],BF_ENABLED);

	    //mCamera->captureRecycleFrame();
	    mPreviewFrame = (previewFrame + 1) % kBufferCount;
	}

	// Notify the client of a new preview frame.
	int postPreviewFrame = mPostPreviewFrame;
	if (isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_ENABLED)
	    && !isBFSet(mPreviewBuffer.flags[postPreviewFrame], BF_LOCKED)) {
	    setBF(&mPreviewBuffer.flags[postPreviewFrame],BF_LOCKED);
	    nsecs_t current_ts = systemTime(SYSTEM_TIME_MONOTONIC);
	    nsecs_t interval_ts = current_ts - mPreviewLastTS;
	    float current_fps, average_fps;
	    mPreviewLastTS = current_ts;
	    current_fps = (float)1000000000 / (float)interval_ts;
	    average_fps = (current_fps + mPreviewLastFPS) / 2;
	    mPreviewLastFPS = current_fps;

	    LOGV("Preview FPS : %2.1f\n", average_fps);
	    LOGV("transfer a preview frame to client (index:%d/%d)",
		 postPreviewFrame, kBufferCount);

            if ((mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME)) {
	        mDataCb(CAMERA_MSG_PREVIEW_FRAME,
		        mPreviewBuffer.base[postPreviewFrame], mCallbackCookie);
            }
	    clrBF(&mPreviewBuffer.flags[postPreviewFrame],BF_LOCKED|BF_ENABLED);
	    mPostPreviewFrame = (postPreviewFrame + 1) % kBufferCount;
	}
    }

    // TODO: Have to change the recordingThread() function to others thread ways
    recordingThread();

    mCamera->captureRecycleFrame();

    return NO_ERROR;
}

status_t CameraHardwareSOC::startPreview()
{
    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }

    int w, h, preview_size;
    mParameters.getPreviewSize(&w, &h);
#ifdef MEMORY_MAP
    CAMLOGD("\n");
    mCamera->captureInit(w, h, mPreviewPixelFormat, 3, V4L2_MEMORY_MMAP, mCameraId);
    mCamera->captureMapFrame();
#else
    CAMLOGD("\n");
    mCamera->captureInit(w, h, mPreviewPixelFormat, 3, V4L2_MEMORY_USERPTR, mCameraId);
    int i;
    void *ptrs[kBufferCount];
    for (i = 0; i < kBufferCount; i++) {
        ptrs[i] = mPreviewBuffer.start[i];
        LOGD("hongyu: ptr[%d] = %p", i, ptrs[i]);
    }
    mCamera->captureSetPtr(mPreviewFrameSize, ptrs);
#endif
    mCamera->captureStart();

    const char *preview_fmt;
    preview_fmt = mParameters.getPreviewFormat();
	
    if (strcmp(preview_fmt, "yuv420sp") == 0) {
	  preview_size = (w * h * 3)/2;
    }  else if (strcmp(preview_fmt, "yuv422i-yuyv") == 0){
	  preview_size = w * h * 2;
    } else if (strcmp(preview_fmt, "rgb565") == 0){
	    //FIXME
	  preview_size = w * h * 2;
    } else {
      LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
      return -1;
    }

    mCameraState = CAM_PREVIEW;
    initHeapLocked(preview_size);

    mPreviewThread = new PreviewThread(this);

    return NO_ERROR;
}

void CameraHardwareSOC::stopPreview()
{
    sp<PreviewThread> previewThread;
    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
      previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);

    if (mPreviewThread != 0) {
      mPreviewThread.clear();
      mCamera->captureStop();
#ifdef MEMORY_MAP      
      mCamera->captureUnmapFrame();
#else
      mCamera->captureUnsetPtr();
#endif
      mCamera->captureFinalize();
    }

    mCameraState = CAM_DEFAULT;
}

bool CameraHardwareSOC::previewEnabled()
{
    return (mPreviewThread != 0);
}

int CameraHardwareSOC::recordingThread()
{

if (!soc_share_buffer_caps_set) {
    unsigned int *frame_id;
    unsigned int frame_num;
    frame_num = mCamera->get_frame_num();
    frame_id = new unsigned int[frame_num];
    mCamera->get_frame_id(frame_id, frame_num);
    mParameters.set_frame_id(frame_id, frame_num);
    delete [] frame_id;
    soc_share_buffer_caps_set=true;
}

    if (mRecordingRunning && (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)) {
        // Get a recording frame
        int recordingFrame = mRecordingFrame;
	int previewFrame = (mPreviewFrame + 3) % kBufferCount;
	if (!isBFSet(mRecordingBuffer.flags[recordingFrame], BF_ENABLED)
	    && !isBFSet(mRecordingBuffer.flags[recordingFrame], BF_LOCKED)) {
#if 0 /* in order to avoid conversion twice with nv21 */
	    mCamera->captureGetRecordingFrame(mRecordingBuffer.start[recordingFrame]);
#else
	    setBF(&mPreviewBuffer.flags[previewFrame], BF_LOCKED);
	    setBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
		#if 0
	    memcpy(mRecordingBuffer.start[recordingFrame],
		   mPreviewBuffer.start[previewFrame], mPreviewFrameSize);
		#endif
		if (mParameters.get_buffer_sharing())
			mCamera->captureGetRecordingFrame(mRecordingBuffer.start[recordingFrame], 1);
		else
			mCamera->captureGetRecordingFrame(mRecordingBuffer.start[recordingFrame], 0);
	    clrBF(&mRecordingBuffer.flags[recordingFrame], BF_LOCKED);
	    clrBF(&mPreviewBuffer.flags[previewFrame], BF_LOCKED);
#endif
	    setBF(&mRecordingBuffer.flags[recordingFrame],BF_ENABLED);
	    mRecordingFrame = (recordingFrame + 1) % kBufferCount;
	}

	// Notify the client of a new recording frame.
	int postRecordingFrame = mPostRecordingFrame;
	if ( !isBFSet(mRecordingBuffer.flags[postRecordingFrame], BF_LOCKED)
	     && isBFSet(mRecordingBuffer.flags[postRecordingFrame], BF_ENABLED)) {
	    nsecs_t current_ts = systemTime(SYSTEM_TIME_MONOTONIC);
	    nsecs_t interval_ts = current_ts - mRecordingLastTS;
	    float current_fps, average_fps;
	    mRecordingLastTS = current_ts;
	    current_fps = (float)1000000000 / (float)interval_ts;
	    average_fps = (current_fps + mRecordingLastFPS) / 2;
	    mRecordingLastFPS = current_fps;

	    LOGV("Recording FPS : %2.1f\n", average_fps);
	    LOGV("transfer a recording frame to client (index:%d/%d) %u",
		 postRecordingFrame, kBufferCount, (unsigned int)current_ts);

	    clrBF(&mRecordingBuffer.flags[postRecordingFrame],BF_ENABLED);
	    setBF(&mRecordingBuffer.flags[postRecordingFrame],BF_LOCKED);

	    mDataCbTimestamp(current_ts, CAMERA_MSG_VIDEO_FRAME,
			     mRecordingBuffer.base[postRecordingFrame], mCallbackCookie);

	    mPostRecordingFrame = (postRecordingFrame + 1) % kBufferCount;
	}
    }
    return NO_ERROR;
}

status_t CameraHardwareSOC::startRecording()
{
    for (int i=0; i < kBufferCount; i++) {
        clrBF(&mPreviewBuffer.flags[i], BF_ENABLED|BF_LOCKED);
        clrBF(&mRecordingBuffer.flags[i], BF_ENABLED|BF_LOCKED);
    }

    mRecordingRunning = true;
    mCameraState = CAM_VID_RECORD;

    return NO_ERROR;
}

void CameraHardwareSOC::stopRecording()
{
    mRecordingRunning = false;
    mCameraState = CAM_PREVIEW;
}

bool CameraHardwareSOC::recordingEnabled()
{
    return mRecordingRunning;
}

void CameraHardwareSOC::releaseRecordingFrame(const sp<IMemory>& mem)
{
    ssize_t offset = mem->offset();
    size_t size = mem->size();
    int releasedFrame = offset / size;

#ifdef RECYCLE_WHEN_RELEASING_RECORDING_FRAME
    unsigned int *buff = (unsigned int *)mem->pointer();

    LOGV(" releaseRecordingFrame : buff = %x ", buff[0]); 
    if(mRecordingRunning) {
           LOGV(" Calls to captureRecycleFrame ");

           mCamera->captureRecycleFrameWithFrameId(*buff);

           LOGV(" Called captureRecycleFrame "); 
    }
#endif

    clrBF(&mRecordingBuffer.flags[releasedFrame], BF_LOCKED);

    LOGV("a recording frame transfered to client has been released (index:%d/%d)",
         releasedFrame, kBufferCount);
}

// ---------------------------------------------------------------------------

int CameraHardwareSOC::beginAutoFocusThread(void *cookie)
{
    CameraHardwareSOC *c = (CameraHardwareSOC *)cookie;
    return c->autoFocusThread();
}

int CameraHardwareSOC::autoFocusThread()
{
    int ret;
    int CID = V4L2_CID_FOCUS_AUTO;
    //ret = mCamera->setCtrl( CID, mBlockNumber, mParameters.get("focus-mode") );
    ret = mCamera->setExtCtrls( CID, mBlockNumber, mParameters.get("focus-mode") );
    if(ret >= 0){
        LOGI("autofocus:auto focus success\n");
        mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);
        return NO_ERROR;
    }
    else{
        LOGE("autofocus:auto focus failed\n");
        mNotifyCb(CAMERA_MSG_FOCUS, false, 0, mCallbackCookie);
        return UNKNOWN_ERROR;
    }


    return NO_ERROR;
}

status_t CameraHardwareSOC::autoFocus()
{
    Mutex::Autolock lock(mLock);
    if (createThread(beginAutoFocusThread, this) == false) {
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t CameraHardwareSOC::cancelAutoFocus()
{   /*
     * TODO:
     */
    return NO_ERROR;
}

/*static*/ int CameraHardwareSOC::beginPictureThread(void *cookie)
{
    CameraHardwareSOC *c = (CameraHardwareSOC *)cookie;
    return c->pictureThread();
}

#define MAX_FRAME_WAIT 20
#define FLASH_FRAME_WAIT 4
int CameraHardwareSOC::pictureThread()
{
    int frame_cnt = 0;
    int frame_wait = MAX_FRAME_WAIT;

    if (mMsgEnabled & CAMERA_MSG_SHUTTER)
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);

    if (mMsgEnabled & CAMERA_MSG_RAW_IMAGE) {
        //FIXME: use a canned YUV image!
        // In the meantime just make another fake camera picture.
      //          mDataCb(CAMERA_MSG_RAW_IMAGE, mBuffer, mCallbackCookie);
    }
    if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
        int w, h;
	mParameters.getPictureSize(&w, &h);

	mCamera->captureInit(w, h, mPreviewPixelFormat, 1, V4L2_MEMORY_MMAP, mCameraId);
	mCamera->captureMapFrame();
	mCamera->captureStart();

	int jpegSize;
	int sensorsize;
	sensorsize = mCamera->captureGrabFrame();
	jpegSize=(sensorsize * 3)/4;

	LOGD(" - JPEG size saved = %dB, %dK",jpegSize, jpegSize/1000);


	sp<MemoryHeapBase> heapSensor = new MemoryHeapBase(sensorsize);
	sp<MemoryBase> bufferSensor = new MemoryBase(heapSensor, 0, sensorsize);
		sp<MemoryHeapBase> heapJpeg = new MemoryHeapBase(jpegSize);
	sp<MemoryBase> bufferJpeg = new MemoryBase(heapJpeg, 0, jpegSize);

	mCamera->captureGetFrame(heapSensor->getBase());

	mCamera->captureRecycleFrame();
	mCamera->captureStop();
	mCamera->captureUnmapFrame();
	mCamera->captureFinalize();


SkImageEncoder::Type fm;
fm = SkImageEncoder::kJPEG_Type;


bool success = false;
//SkMemoryStream * stream = new SkMemoryStream(jpegSize);
SkMemoryWStream *stream =new SkMemoryWStream(heapJpeg->getBase(),jpegSize);

SkBitmap *bitmap =new SkBitmap();
bitmap->setConfig(SkBitmap::kRGB_565_Config, w,h);

	bitmap->setPixels(heapSensor->getBase(),NULL);



	SkImageEncoder* encoder = SkImageEncoder::Create(fm);
	if (NULL != encoder) {
		success = encoder->encodeStream(stream, *bitmap, 75);
//		success = encoder->encodeFile("/data/tmp.jpg", *bitmap, 75);
//		LOGD(" sucess is %d",success);
		delete encoder;
	}

//	mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, buffer, mCallbackCookie);
	mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, bufferJpeg, mCallbackCookie);

    }
    return NO_ERROR;
}

status_t CameraHardwareSOC::takePicture()
{
    disableMsgType(CAMERA_MSG_PREVIEW_FRAME);
    stopPreview();
    mCameraState = CAM_PIC_SNAP;

    if (createThread(beginPictureThread, this) == false)
        return -1;

    return NO_ERROR;
}

status_t CameraHardwareSOC::cancelPicture()
{
     return NO_ERROR;
}

status_t CameraHardwareSOC::dump(int fd, const Vector<String16>& args) const
{
    LOGD("%s",__func__);
    return NO_ERROR;
}

status_t CameraHardwareSOC::setParameters(const CameraParameters& params)
{
    Mutex::Autolock lock(mLock);

    int CID;
    CameraParameters p = params;

    int preview_size;
    int preview_width,preview_height;
    p.getPreviewSize(&preview_width,&preview_height);
    p.setPreviewSize(preview_width,preview_height);

    int new_fps = p.getPreviewFrameRate();
    int set_fps = mParameters.getPreviewFrameRate();
    if (new_fps != set_fps) {
        p.setPreviewFrameRate(new_fps);
	LOGD("     ++ Changed FPS to %d",p.getPreviewFrameRate());
    }

    const char *new_value, *set_value;
    const char *newKey, *setKey;
    int newValue, setValue;

    new_value = p.getPreviewFormat();
    set_value = mParameters.getPreviewFormat();
    if (strcmp(new_value, "yuv420sp") == 0) {
      mPreviewPixelFormat = V4L2_PIX_FMT_NV12;
	  preview_size = (preview_width * preview_height * 3)/2;
    }  else if (strcmp(new_value, "yuv422i-yuyv") == 0){
      mPreviewPixelFormat = V4L2_PIX_FMT_YUYV;
	  preview_size = preview_width * preview_height * 2;
    } else if (strcmp(new_value, "rgb565") == 0){
      mPreviewPixelFormat = V4L2_PIX_FMT_RGB565;
	  preview_size = preview_width * preview_height * 2;
    } else {
      LOGE("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported");
      return -1;
    }
    if (strcmp(set_value, new_value)) {
      p.setPreviewFormat(new_value);
      LOGD("     ++ Changed Preview Pixel Format to %s",p.getPreviewFormat());
    }

    new_value = p.getPictureFormat();
    set_value = mParameters.getPictureFormat();
    if (strcmp(new_value, "jpeg") == 0)
	/*
	 * TODO:FIX ME-----if JPEG is supportted by ISP or sensorsoc,
	 *  should be V4L2_PIX_FMT_JPEG
	 */
        mPicturePixelFormat = V4L2_PIX_FMT_RGB565;
    else {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    if (strcmp(set_value, new_value)) {
      p.setPictureFormat(new_value);
      LOGD("     ++ Changed Preview Pixel Format to %s",p.getPictureFormat());
    }

    if ( (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) || (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) ) {
        
        if( NULL != (*mSensorNow)->jpegquality_map ){
            newKey = p.get("jpeg-quality");
            newValue = p.getInt(newKey);
            setKey = mParameters.get("jpeg-quality");
            setValue = mParameters.getInt(setKey);
            if( strcmp(setKey, newKey) ) {
	        /* 
   	         * TODO....
	         */
                if(setValue >= 90)
                    setValue = 1; 
                else if(setValue >= 80)
                    setValue = 2; 	    
                else 
                    setValue = 3;
                if(newValue >= 90)
                    newValue = 1; //? communicate with driver
                else if(newValue >= 80)
                    newValue = 2; //? communicate with driver	    
                else 
                    newValue = 3; //? communicate with driver
                LOGD("     ++ Changed jpeg-quality from %s(%d) to %s(%d)", setKey, setValue, newKey, newValue);
#if 0
                CID = ; //? communicate with driver
                if(mCamera->setCtrl(CID, newValue, newKey) < 0){
                    LOGE("set jpegquality failed\n");
                }
#endif
            }
        }

        if( NULL != (*mSensorNow)->effect_map ){
            newKey = p.get("effect");
            newValue = p.getInt(newKey);
            setKey= mParameters.get("effect");
            setValue = mParameters.getInt(setKey);
            if (strcmp(setKey, newKey)) {
                LOGD("     ++ Changed effect from %s(%d) to %s(%d)", setKey, setValue, newKey, newValue);
                CID = V4L2_CID_COLORFX;
                if(mCamera->setCtrl(CID, newValue, newKey) < 0){
                    LOGE("set ColorEffect failed\n");
                }
            }
        }

        if( NULL != (*mSensorNow)->wb_map ){
            newKey = p.get("whitebalance");
            newValue = p.getInt(newKey);
            setKey = mParameters.get("whitebalance");
            setValue = mParameters.getInt(setKey);
            if (strcmp(setKey, newKey)) {
                LOGD("     ++ Changed whitebalance from %s(%d) to %s(%d)",setKey, setValue, newKey, newValue);
                CID = V4L2_CID_WHITE_BALANCE_TEMPERATURE;
                if(mCamera->setCtrl(CID, newValue, newKey) < 0){
                    LOGE("set whitebalance failed\n");
                }
            }
        }

        if( NULL != (*mSensorNow)->exposure_map ){
            newKey = p.get("exposure-compensation");
            newValue = p.getInt("exposure-compensation");
            setKey = mParameters.get("exposure-compensation");
            setValue = mParameters.getInt("exposure-compensation");
            if (strcmp(setKey, newKey)) {
                CID = V4L2_CID_EXPOSURE;
                LOGD("     ++ Changed exposure-compensation from %s(%d) to %s(%d)",setKey, setValue, newKey, newValue);
                if(mCamera->setCtrl(CID, newValue, newKey) < 0){
                    LOGE("set exposure failed\n");
                }
            }
        }

        newKey = p.get("focus-mode");
        newValue = p.getInt(newKey);
        setKey = mParameters.get("focus-mode");
        setValue = mParameters.getInt(setKey);
        if( !strcmp(CameraParameters::FOCUS_MODE_AUTO, newKey) ){
            mBlockNumber = 0;
        }
        else if( !strcmp(FOCUS_MODE_TOUCHED, newKey) ){
            mBlockNumber = newValue;
        }
        LOGD("Changed focus-mode from %s(%d) to %s(%d), mBlockNumber = %d\n",
                setKey, setValue, newKey, newValue, mBlockNumber);
#if 0
        /*
         * TODO: maybe to support some other focus mode, for example "infinity"
         */
        if( strcmp(setKey, newKey) ) {
            LOGD("     ++ Changed focus-mode from %s(%d) to %s(%d)",setKey, setValue, newKey, newValue);
            if( !strcmp(CameraParameters::FOCUS_MODE_AUTO, newKey) ){
                CID = ; //? communicate with driver to change focus mode
                if(mCamera->setCtrl(CID, newValue, newKey) < 0){
                    LOGE("set autofoucs mode failed\n");
                }
            }
            else if( !strcmp(CameraParameters::FOCUS_MODE_INFINITY, newkey) ){
                CID = ; //? communicate with driver to change focus mode
                if(mCamera->setCtrl(CID, newValue, newKey) < 0){
                    LOGE("set infinity mode failed\n");
                }
            }
        }
#endif

#if 0
        /*
         * TODO: maybe need to fix somewhat about rotation
         */
        if( NULL != (*mSensorNow)->rotation_map ){
            newKey = p.get("rotation");
            newValue = p.getInt(newKey);
            setKey = mParameters.get("rotation");
            setValue = mParameters.getInt(setKey);
            if (strcmp(setKey, newKey)) {
                LOGD("     ++ Changed rotation from %s(%d) to %s(%d)",setKey, setValue, newKey, newValue);
            }
        }
#endif
        if( NULL != (*mSensorNow)->flashmode_map ){
            newKey = p.get("flash-mode");
            newValue = p.getInt(newKey);
            setKey = mParameters.get("flash-mode");
            setValue = mParameters.getInt(setKey);
            if (strcmp(setKey, newKey)) {
                LOGD("     ++ Changed flash-mode from %s(%d)to %s(%d)", setKey, setValue, newKey, newValue);
                //mCamera->setFlash(p.get("flash-mode"));
            }
        }

    }

    mParameters = p;

    initHeapLocked(preview_size);

    return NO_ERROR;
}

CameraParameters CameraHardwareSOC::getParameters() const
{
    Mutex::Autolock lock(mLock);
    return mParameters;
}

status_t CameraHardwareSOC::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHardwareSOC::release()
{
}

wp<CameraHardwareInterface> CameraHardwareSOC::singleton;

sp<CameraHardwareInterface> CameraHardwareSOC::createInstance(int cameraId)
{
    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }
    sp<CameraHardwareInterface> hardware(new CameraHardwareSOC(cameraId));
    singleton = hardware;
    return hardware;
}

}; // namespace android
