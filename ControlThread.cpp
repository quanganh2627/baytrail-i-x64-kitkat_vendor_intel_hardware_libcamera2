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
#define LOG_TAG "Atom_ControlThread"

#include "ControlThread.h"
#include "LogHelper.h"
#include "PreviewThread.h"
#include "PictureThread.h"
#include "AtomISP.h"
#include "Callbacks.h"

namespace android {

ControlThread::ControlThread(int cameraId) :
    Thread(true) // callbacks may call into java
    ,mISP(new AtomISP(cameraId))
    ,mPreviewThread(new PreviewThread((ICallbackPreview *) this))
    ,mPictureThread(new PictureThread((ICallbackPicture *) this))
    ,mMessageQueue("ControlThread", (int) MESSAGE_ID_MAX)
    ,mState(STATE_STOPPED)
    ,mThreadRunning(false)
    ,mCallbacks(new Callbacks())
    ,mNumPreviewFramesOut(0)
    ,mNumRecordingFramesOut(0)
{
    LOG_FUNCTION
    int ret, camera_idx = -1;
    mCameraId = cameraId;
    LogDetail("mCameraId = %d", mCameraId);

    mPictureThread->setPicturePixelFormat(V4L2_PIX_FMT_YUV420);

    initDefaultParameters();
}

ControlThread::~ControlThread()
{
    LOG_FUNCTION
    mPreviewThread.clear();
    mPictureThread.clear();
    if (mISP != NULL) {
        delete mISP;
    }
    if (mCallbacks != NULL) {
        delete mCallbacks;
    }
}

void ControlThread::initDefaultParameters()
{
    LOG_FUNCTION
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
    sprintf(pchar, "%d", PictureThread::getDefaultJpegQuality());
    p.set(CameraParameters::KEY_JPEG_QUALITY, pchar);
    sprintf(pchar, "%d", PictureThread::getDefaultThumbnailQuality());
    p.set(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY, pchar);

    const char *resolution_dec = mISP->getMaxSnapShotResolution();
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, resolution_dec);
    int ww,hh;
    mISP->getMaxSnapshotSize(&ww,&hh);
    mISP->setSnapshotFrameFormat(ww, hh, mPictureThread->getPicturePixelFormat());
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

    p.setVideoSize(1280,720);
    p.set(CameraParameters::KEY_PREFERRED_PREVIEW_SIZE_FOR_VIDEO, "640x480");
    p.set(CameraParameters::KEY_SUPPORTED_VIDEO_SIZES, "640x480,1280x720,1920x1080");
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

    mParameters = p;
}

status_t ControlThread::setPreviewWindow(struct preview_stream_ops *window)
{
    LOG_FUNCTION
    LogDetail("window = %p", window);
    if (window != NULL && mPreviewThread != NULL) {
        return mPreviewThread->setPreviewWindow(window);
    }
    return NO_ERROR;
}

void ControlThread::setCallbacks(camera_notify_callback notify_cb,
                                 camera_data_callback data_cb,
                                 camera_data_timestamp_callback data_cb_timestamp,
                                 camera_request_memory get_memory,
                                 void* user)
{
    LOG_FUNCTION
    mCallbacks->setCallbacks(notify_cb,
            data_cb,
            data_cb_timestamp,
            get_memory,
            user);
    mISP->setCallbacks(mCallbacks);
    mPreviewThread->setCallbacks(mCallbacks);
    mPictureThread->setCallbacks(mCallbacks);
}

void ControlThread::enableMsgType(int32_t msgType)
{
    LOG_FUNCTION
    mCallbacks->enableMsgType(msgType);
}

void ControlThread::disableMsgType(int32_t msgType)
{
    LOG_FUNCTION
    mCallbacks->disableMsgType(msgType);
}

bool ControlThread::msgTypeEnabled(int32_t msgType)
{
    return mCallbacks->msgTypeEnabled(msgType);
}

status_t ControlThread::startPreview()
{
    LOG_FUNCTION
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_START_PREVIEW;
    return mMessageQueue.send(&msg, MESSAGE_ID_START_PREVIEW);
}

status_t ControlThread::stopPreview()
{
    LOG_FUNCTION
    // send message and block until thread processes message
    if(mState == STATE_STOPPED){
        return NO_ERROR;
    }

    Message msg;
    msg.id = MESSAGE_ID_STOP_PREVIEW;
    return mMessageQueue.send(&msg, MESSAGE_ID_STOP_PREVIEW);
}

status_t ControlThread::startRecording()
{
    LOG_FUNCTION
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_START_RECORDING;
    return mMessageQueue.send(&msg, MESSAGE_ID_START_RECORDING);
}

status_t ControlThread::stopRecording()
{
    LOG_FUNCTION
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_STOP_RECORDING;
    return mMessageQueue.send(&msg, MESSAGE_ID_STOP_RECORDING);
}

bool ControlThread::previewEnabled()
{
    return mState == STATE_PREVIEW_VIDEO || mState == STATE_PREVIEW_STILL;
}

bool ControlThread::recordingEnabled()
{
    return mState == STATE_RECORDING;
}

status_t ControlThread::setParameters(const char *params)
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_SET_PARAMETERS;
    msg.data.setParameters.params = const_cast<char*>(params); // We swear we won't modify params :)
    return mMessageQueue.send(&msg, MESSAGE_ID_SET_PARAMETERS);
}

void ControlThread::setISPParameters(
        const CameraParameters &new_params,
        const CameraParameters &old_params)
{
    LOG_FUNCTION
    // TODO: add ISP parameters

    //process zoom
    int zoom = new_params.getInt(CameraParameters::KEY_ZOOM);
    mISP->setZoom(zoom);
}

char* ControlThread::getParameters()
{
    LOG_FUNCTION

    char *params = NULL;
    Message msg;
    msg.id = MESSAGE_ID_GET_PARAMETERS;
    msg.data.getParameters.params = &params; // let control thread allocate and set pointer
    mMessageQueue.send(&msg, MESSAGE_ID_GET_PARAMETERS);
    return params;
}

void ControlThread::putParameters(char* params)
{
    LOG_FUNCTION
    if (params)
        free(params);
}

bool ControlThread::isParameterSet(const char* param)
{
    const char* strParam = mParameters.get(param);
    int len = strlen(CameraParameters::TRUE);
    if (strParam != NULL && strncmp(strParam, CameraParameters::TRUE, len) == 0) {
        return true;
    }
    return false;
}

status_t ControlThread::takePicture()
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_TAKE_PICTURE;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::cancelPicture()
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_PICTURE;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::autoFocus()
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::cancelAutoFocus()
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::releaseRecordingFrame(void *buff)
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_RELEASE_RECORDING_FRAME;
    msg.data.releaseRecordingFrame.buff = buff;
    return mMessageQueue.send(&msg);
}

void ControlThread::previewDone(AtomBuffer *buff)
{
    LOG_FUNCTION2
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW_DONE;
    msg.data.previewDone.buff = buff;
    mMessageQueue.send(&msg);
}

void ControlThread::pictureDone(AtomBuffer *buff)
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_PICTURE_DONE;
    msg.data.pictureDone.buff = buff;
    mMessageQueue.send(&msg);
}

status_t ControlThread::handleMessageExit()
{
    LOG_FUNCTION
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return NO_ERROR;
}

status_t ControlThread::handleMessageStartPreview()
{
    LOG_FUNCTION
    status_t status;
    if (mState == STATE_STOPPED) {
        status = mPreviewThread->run();
        if (status == NO_ERROR) {
            State preState;
            if (isParameterSet(CameraParameters::KEY_RECORDING_HINT)) {
                preState = STATE_PREVIEW_VIDEO;
                status = mISP->start(AtomISP::MODE_VIDEO);
                LogDetail("Starting camera in PREVIEW_VIDEO mode");
            } else {
                preState = STATE_PREVIEW_STILL;
                status = mISP->start(AtomISP::MODE_PREVIEW);
                LogDetail("Starting camera in PREVIEW_STILL mode");
            }

            if (status == NO_ERROR) {
                mState = preState;
                mNumPreviewFramesOut = 0;
            }
        } else {
            LogError("Error starting preview thread");
        }
    } else {
        LogError("Error starting preview. Invalid state!");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_START_PREVIEW, status);
    return status;
}

status_t ControlThread::handleMessageStopPreview()
{
    LOG_FUNCTION
    status_t status;
    if (mState != STATE_STOPPED) {
        status = mPreviewThread->requestExitAndWait();
        if (status == NO_ERROR) {
            status = mISP->stop();
            if (status == NO_ERROR)
                mState = STATE_STOPPED;
        } else {
            LogError("Error stopping preview thread");
        }
    } else {
        LogError("Error stopping preview. Invalid state!");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_STOP_PREVIEW, status);
    return status;
}

status_t ControlThread::handleMessageStartRecording()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    if (mState == STATE_PREVIEW_VIDEO) {
        mState = STATE_RECORDING;
    } else if (mState == STATE_PREVIEW_STILL) {
        /* We are in PREVIEW_STILL mode; in order to start recording
         * we first need to stop AtomISP and restart it with MODE_VIDEO
         */
        LogDetail("We are in STATE_PREVIEW. Switching to STATE_VIDEO before starting to record.");
        if ((status = mISP->stop()) == NO_ERROR) {
            if ((status = mISP->start(AtomISP::MODE_VIDEO)) == NO_ERROR) {
                mState = STATE_RECORDING;
            } else {
                LogError("Error starting ISP in VIDEO mode!");
            }
        } else {
            LogError("Error stopping ISP!");
        }
    } else {
        LogError("Error starting recording. Invalid state!");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_START_RECORDING, status);
    return status;
}

status_t ControlThread::handleMessageStopRecording()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    if (mState == STATE_RECORDING) {
        /*
         * Even if startRecording was called from PREVIEW_STILL mode, we can
         * switch back to PREVIEW_VIDEO now since we got a startRecording
         */
        mState = STATE_PREVIEW_VIDEO;
        mNumRecordingFramesOut = 0;
    } else {
        LogError("Error stopping recording. Invalid state!");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_STOP_RECORDING, status);
    return status;
}

status_t ControlThread::handleMessageTakePicture()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    /* TODO: implement later
    FrameInfo snapshot;
    FrameInfo preview;
    FrameInfo postview;

    snapshot = mISP->getSnapshotFrameFormat();
    preview = mISP->getPreviewFrameFormat();

    //Postview size should be smaller
    mISP->setPostviewFrameFormat(preview.width>>1, preview.height>>1, preview.format);
    postview = mISP->getPostviewFrameFormat();

    if (msgTypeEnabled(CAMERA_MSG_COMPRESSED_IMAGE)) {
        int index;
        void *main_out, *postview_out;
        void *pmainimage;
        void *pthumbnail; // first save RGB565 data, then save jpeg encoded data into this pointer
        AtomBuffer mainBuff, postviewBuff;
        unsigned int page_size = getpagesize();
        unsigned int size_aligned = (preview.padding + page_size - 1) & ~(page_size - 1);
        unsigned int postview_size = size_aligned;
        unsigned int rgb_frame_size = snapshot.width * snapshot.height * 2;
        unsigned int mainsize_aligned = (rgb_frame_size + page_size - 1) & ~(page_size - 1);
        static const unsigned exif_offset =  64*1024;  // must page size aligned, exif must less 64KBytes
        static const unsigned thumbnail_offset = 600*1024; // must page size aligned, max is 640*480*2
        unsigned total_size = mainsize_aligned + exif_offset + thumbnail_offset;

        mCallbacks->allocateMemory(&postviewBuff, postview_size);
        mCallbacks->allocateMemory(&mainBuff, total_size);
        pthumbnail = (void*)((char*)(mainBuff.buff->data) + exif_offset);
        pmainimage = (void*)((char*)(mainBuff.buff->data) + exif_offset + thumbnail_offset);
        mISP->setSnapshotUserptr(0, pmainimage, postviewBuff.buff->data);
        status = mISP->start(AtomISP::MODE_CAPTURE);

        if (status == NO_ERROR)
            mState = STATE_CAPTURE;
    }
    */
    return status;
}

status_t ControlThread::handleMessageCancelPicture()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t ControlThread::handleMessageAutoFocus()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    // TODO: implement
    mCallbacks->autofocusDone();
    return status;
}

status_t ControlThread::handleMessageCancelAutoFocus()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    void *buff;

    // TODO: implement

    return status;
}

status_t ControlThread::handleMessageReleaseRecordingFrame(MessageReleaseRecordingFrame *msg)
{
    LOG_FUNCTION2
    status_t status = mISP->putRecordingFrame(msg->buff);
    if (status == NO_ERROR)
        mNumRecordingFramesOut--;
    else
        LogError("Error putting recording frame to ISP");
    return status;
}

status_t ControlThread::handleMessagePreviewDone(MessagePreviewDone *msg)
{
    LOG_FUNCTION2
    status_t status = mISP->putPreviewFrame(msg->buff);
    if (status == NO_ERROR)
        mNumPreviewFramesOut--;
    else
        LogError("Error putting preview frame to ISP");
    return status;
}

status_t ControlThread::handleMessagePictureDone(MessagePictureDone *msg)
{
    LOG_FUNCTION
    // TODO: implement
    return NO_ERROR;
}

status_t ControlThread::handleMessageSetParameters(MessageSetParameters *msg)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    int preview_width, preview_height;
    int picture_width, picture_height;
    int recording_width, recording_height;
    int min_fps, max_fps;
    int preview_format = 0;
    int recording_format = 0;
    int zoom;
    const char *new_value, *set_value;
    CameraParameters params;

    String8 str_params(msg->params);
    params.unflatten(str_params);
    params.dump();  // print parameters for debug

    CameraParameters p = params;

    p.getPreviewSize(&preview_width, &preview_height);
    new_value = p.getPreviewFormat();
    set_value = mParameters.getPreviewFormat();

    if (new_value == NULL) {
        LogError("Preview format not found!");
        return UNKNOWN_ERROR;
    }

    int len = strlen(new_value);
    if (strncmp(new_value, CameraParameters::PIXEL_FORMAT_YUV420SP, len) == 0) {
        preview_format = V4L2_PIX_FMT_NV12;
    }  else if (strncmp(new_value, CameraParameters::PIXEL_FORMAT_YUV422I, len) == 0) {
        preview_format = V4L2_PIX_FMT_YUYV;
    } else if (strncmp(new_value, CameraParameters::PIXEL_FORMAT_RGB565, len) == 0) {
        preview_format = V4L2_PIX_FMT_RGB565;
    } else {
        LogDetail("Only yuv420sp, yuv422i-yuyv, rgb565 preview are supported, use rgb565");
        preview_format = V4L2_PIX_FMT_RGB565;
    }

    if (0 < preview_width && 0 < preview_height) {
        mPreviewThread->setPreviewSize(preview_width, preview_height);
        LogDetail(" - Preview pixel format = new \"%s\"  / current \"%s\"",
            new_value, set_value);
        if (mISP->setPreviewFrameFormat(preview_width, preview_height,
                                    preview_format) < 0) {
            LogError("Fail on setPreviewSize(width(%d), height(%d), format(%d))",
                     preview_width, preview_height, preview_format);
        } else {
            p.setPreviewSize(preview_width, preview_height);
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
    LogDetail("PREVIEW SIZE: %dx%d, FPS: %d", preview_width, preview_height,
            new_fps);

    //Picture format
    const char *new_format = p.getPictureFormat();
    if (strncmp(new_format, "jpeg", strlen(new_format)) == 0)
        mPictureThread->setPicturePixelFormat(V4L2_PIX_FMT_YUV420);
    else {
        LogDetail("Only jpeg still pictures are supported, new_format:%s", new_format);
    }
    LogDetail(" - Picture pixel format = new \"%s\"", new_format);
    p.getPictureSize(&picture_width, &picture_height);

    LogDetail("picture_width %d picture_height = %d",
         picture_width, picture_height);

    if (0 < picture_width && 0 < picture_height) {
        if (mISP->setSnapshotFrameFormat(picture_width, picture_height,
                mPictureThread->getPicturePixelFormat()) < 0) {
            LogError("Fail on mISP->setSnapshotSize(width(%d), height(%d))",
                picture_width, picture_height);
            status = UNKNOWN_ERROR;
            goto exit;
        } else {
            p.setPictureSize(picture_width, picture_height);
            p.setPictureFormat(new_value);
        }
    }

    //Zoom is a invalid value or not
    zoom = p.getInt(CameraParameters::KEY_ZOOM);
    if(zoom > MAX_ZOOM_LEVEL || zoom < MIN_ZOOM_LEVEL) {
        status = BAD_VALUE;
        goto exit;
    }

    // preview fps range is a invalid value range or not
    p.getPreviewFpsRange(&min_fps, &max_fps);
    if(min_fps == max_fps || min_fps > max_fps) {
        status = BAD_VALUE;
        goto exit;
    }

    p.getVideoSize(&recording_width, &recording_height);
    mISP->setVideoFrameFormat(recording_width, recording_height, V4L2_PIX_FMT_NV12);

    p.set(CameraParameters::KEY_ZOOM_SUPPORTED, "true");

    setISPParameters(p, mParameters);

    //Update the parameters
    mParameters = p;

exit:
    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_SET_PARAMETERS, status);
    return status;
}

status_t ControlThread::handleMessageGetParameters(MessageGetParameters *msg)
{
    LOG_FUNCTION
    status_t status = BAD_VALUE;

    if (msg->params) {
        String8 params = mParameters.flatten();
        int len = params.length();
        *msg->params = strndup(params.string(), sizeof(char) * len);
        status = NO_ERROR;
    }
    mMessageQueue.reply(MESSAGE_ID_GET_PARAMETERS, status);
    return status;
}

status_t ControlThread::waitForAndExecuteMessage()
{
    LOG_FUNCTION2
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_START_PREVIEW:
            status = handleMessageStartPreview();
            break;

        case MESSAGE_ID_STOP_PREVIEW:
            status = handleMessageStopPreview();
            break;

        case MESSAGE_ID_START_RECORDING:
            status = handleMessageStartRecording();
            break;

        case MESSAGE_ID_STOP_RECORDING:
            status = handleMessageStopRecording();
            break;

        case MESSAGE_ID_TAKE_PICTURE:
            status = handleMessageTakePicture();
            break;

        case MESSAGE_ID_CANCEL_PICTURE:
            status = handleMessageCancelPicture();
            break;

        case MESSAGE_ID_AUTO_FOCUS:
            status = handleMessageAutoFocus();
            break;

        case MESSAGE_ID_CANCEL_AUTO_FOCUS:
            status = handleMessageCancelAutoFocus();
            break;

        case MESSAGE_ID_RELEASE_RECORDING_FRAME:
            status = handleMessageReleaseRecordingFrame(&msg.data.releaseRecordingFrame);
            break;

        case MESSAGE_ID_PREVIEW_DONE:
            status = handleMessagePreviewDone(&msg.data.previewDone);
            break;

        case MESSAGE_ID_PICTURE_DONE:
            status = handleMessagePictureDone(&msg.data.pictureDone);
            break;

        case MESSAGE_ID_SET_PARAMETERS:
            status = handleMessageSetParameters(&msg.data.setParameters);
            break;

        case MESSAGE_ID_GET_PARAMETERS:
            status = handleMessageGetParameters(&msg.data.getParameters);
            break;

        default:
            LogError("Invalid message");
            status = BAD_VALUE;
            break;
    };

    if (status != NO_ERROR)
        LogError("Error handling message %d", (int) msg.id);
    return status;
}

bool ControlThread::ispHasData()
{
    // For video/recording, make sure isp has a preview and a recording buffer
    if (mState == STATE_PREVIEW_VIDEO || mState == STATE_RECORDING)
        if (mNumRecordingFramesOut == ATOM_RECORDING_BUFFERS)
            return false;

    // For preview, just make sure we isp has a preview buffer
    if (mNumPreviewFramesOut == ATOM_PREVIEW_BUFFERS)
        return false;

    return true;
}

status_t ControlThread::dequeuePreview()
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    AtomBuffer *buff;
    status_t status = NO_ERROR;

    status = mISP->getPreviewFrame(&buff);
    if (status == NO_ERROR) {
        status = mPreviewThread->preview(buff);
        if (status == NO_ERROR)
            mNumPreviewFramesOut++;
        else
            LogError("Error sending buffer to preview thread");
    } else {
        LogError("Error gettting preview frame from ISP");
    }
    return status;
}

status_t ControlThread::dequeueRecording()
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    AtomBuffer *buff;
    nsecs_t timestamp;
    status_t status = NO_ERROR;

    status = mISP->getRecordingFrame(&buff, &timestamp);
    if (status == NO_ERROR) {

        // See if recording has started.
        // If it has, process the buffer
        // If it hasn't, return the buffer to the driver
        if (mState == STATE_RECORDING) {
            mCallbacks->videoFrameDone(buff, timestamp);
            mNumRecordingFramesOut++;
        } else {
            mISP->putRecordingFrame(buff->buff->data);
        }
    } else {
        LogError("Error: getting recording from isp\n");
    }

    return status;
}

bool ControlThread::threadLoop()
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning) {

        switch (mState) {

        case STATE_STOPPED:
            LogDetail2("In STATE_STOPPED...");
            // in the stop state all we do is wait for messages
            status = waitForAndExecuteMessage();
            break;

        case STATE_PREVIEW_STILL:
            LogDetail2("In STATE_PREVIEW_STILL...");
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                // make sure ISP has data before we ask for some
                if (ispHasData())
                    status = dequeuePreview();
                else
                    status = waitForAndExecuteMessage();
            }
            break;

        case STATE_PREVIEW_VIDEO:
        case STATE_RECORDING:
            LogDetail2("In %s...", mState == STATE_PREVIEW_VIDEO ? "STATE_PREVIEW_VIDEO" : "STATE_RECORDING");
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                // make sure ISP has data before we ask for some
                if (ispHasData()) {
                    status = dequeuePreview();
                    if (status == NO_ERROR)
                        status = dequeueRecording();
                } else {
                    status = waitForAndExecuteMessage();
                }
            }
            break;

        default:
            break;
        };
    }

    return false;
}

status_t ControlThread::requestExitAndWait()
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_EXIT;

    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

} // namespace android
