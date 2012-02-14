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
#include "ColorConverter.h"

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
{
    LOG_FUNCTION
    LogDetail("cameraId= %d", cameraId);

    memset(mCoupledBuffers, 0, sizeof(mCoupledBuffers));

    // get default params from AtomISP and JPEG encoder
    mISP->getDefaultParameters(&mParameters);
    mPictureThread->getDefaultParameters(&mParameters);
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

status_t ControlThread::setPreviewWindow(struct preview_stream_ops *window)
{
    LOG_FUNCTION
    LogDetail("window = %p", window);
    if (mPreviewThread != NULL) {
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
    return mState != STATE_STOPPED;
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

void ControlThread::pictureDone(AtomBuffer *snapshotBuf, AtomBuffer *postviewBuf)
{
    LOG_FUNCTION
    Message msg;
    msg.id = MESSAGE_ID_PICTURE_DONE;
    msg.data.pictureDone.snapshotBuf = snapshotBuf;
    msg.data.pictureDone.postviewBuf = postviewBuf;
    mMessageQueue.send(&msg);
}

status_t ControlThread::handleMessageExit()
{
    LOG_FUNCTION
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return NO_ERROR;
}

status_t ControlThread::startPreviewCore(bool videoMode)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    int width;
    int height;
    int format;
    State state;
    AtomISP::Mode mode;

    if (mState != STATE_STOPPED) {
        LogError("must be in stop state to start preview");
        return INVALID_OPERATION;
    }

    if (videoMode) {
        LogDetail("Starting preview in video mode");
        state = STATE_PREVIEW_VIDEO;
        mode = AtomISP::MODE_VIDEO;
    } else {
        LogDetail("Starting preview in still mode");
        state = STATE_PREVIEW_STILL;
        mode = AtomISP::MODE_PREVIEW;
    }

    // set preview frame config
    format = V4L2Format(mParameters.getPreviewFormat());
    if (format == -1) {
        LogError("bad preview format");
        return BAD_VALUE;
    }
    mParameters.getPreviewSize(&width, &height);
    mISP->setPreviewFrameFormat(width, height, format);
    mPreviewThread->setPreviewSize(width, height);

    // set video frame config
    if (videoMode) {
        format = V4L2Format(mParameters.get(CameraParameters::KEY_VIDEO_FRAME_FORMAT));
        if (format == -1) {
            LogError("bad video format");
            return BAD_VALUE;
        }
        mParameters.getVideoSize(&width, &height);
        mISP->setVideoFrameFormat(width, height, format);
    }

    // start the data flow
    status = mISP->start(mode);
    if (status == NO_ERROR) {
        status = mPreviewThread->run();
        if (status == NO_ERROR) {
            memset(mCoupledBuffers, 0, sizeof(mCoupledBuffers));
            mState = state;
        } else {
            LogError("Error starting preview thread");
            mISP->stop();
        }
    } else {
        LogError("Error starting isp");
    }

    return status;
}

status_t ControlThread::stopPreviewCore()
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    mPreviewThread->requestExitAndWait();
    status = mISP->stop();
    if (status == NO_ERROR)
        mState = STATE_STOPPED;
    else
        LogError("Error stopping isp in preview mode");
    return status;
}

status_t ControlThread::restartPreview(bool videoMode)
{
    status_t status = stopPreviewCore();
    if (status == NO_ERROR)
        status = startPreviewCore(videoMode);
    return status;
}

status_t ControlThread::handleMessageStartPreview()
{
    LOG_FUNCTION
    status_t status;
    if (mState == STATE_STOPPED) {
        bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;
        status = startPreviewCore(videoMode);
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
        status = stopPreviewCore();
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
    AtomBuffer *snapshotBuffer, *postviewBuffer;
    int width;
    int height;
    int format;

    if (mState != STATE_STOPPED) {
        status = mPreviewThread->requestExitAndWait();
        if (status == NO_ERROR) {
            status = mISP->stop();
            if (status == NO_ERROR) {
                mState = STATE_STOPPED;
            }
        } else {
            LogError("Error stopping preview thread");
            return status;
        }
    }

    // configure snapshot
    mParameters.getPictureSize(&width, &height);
    format = mISP->getSnapshotPixelFormat();
    mISP->setSnapshotFrameFormat(width, height, format);
    mPictureThread->setPictureFormat(format);
    mPictureThread->initialize(mParameters, false);

    if ((status = mISP->start(AtomISP::MODE_CAPTURE)) != NO_ERROR) {
        LogError("Error starting the ISP driver in CAPTURE mode!");
        return status;
    }

    // Get the snapshot
    if ((status = mISP->getSnapshot(&snapshotBuffer, &postviewBuffer)) != NO_ERROR) {
        LogError("Error in grabbing snapshot!");
        return status;
    }


    // tell CameraService to play the shutter sound
    mCallbacks->shutterSound();

    // Start PictureThread
    status = mPictureThread->run();
    if (status == NO_ERROR) {
        status = mPictureThread->encode(snapshotBuffer, postviewBuffer);
    } else {
        LogError("Error starting PictureThread!");
    }

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
    status_t status = NO_ERROR;
    if (mState == STATE_RECORDING) {
        AtomBuffer *recBuff = findRecordingBuffer(msg->buff);
        if (recBuff == NULL) {
            // This should NOT happen
            LogError("Could not find recording buffer: %p", msg->buff);
            return DEAD_OBJECT;
        }
        int curBuff = recBuff->id;
        mCoupledBuffers[curBuff].recordingBuffReturned = true;
        if (mCoupledBuffers[curBuff].previewBuffReturned) {
            status = queueCoupledBuffers(curBuff);
        }
    }
    return status;
}

status_t ControlThread::handleMessagePreviewDone(MessagePreviewDone *msg)
{
    LOG_FUNCTION2
    status_t status = NO_ERROR;
    if (mState == STATE_PREVIEW_STILL) {
        status = mISP->putPreviewFrame(msg->buff);
        if (status == DEAD_OBJECT) {
            LogDetail("Stale preview buffer returned to ISP");
        } else if (status != NO_ERROR) {
            LogError("Error putting preview frame to ISP");
        }
    } else if (mState == STATE_PREVIEW_VIDEO || mState == STATE_RECORDING) {
        int curBuff = msg->buff->id;
        mCoupledBuffers[curBuff].previewBuffReturned = true;
        if (mCoupledBuffers[curBuff].recordingBuffReturned) {
            status = queueCoupledBuffers(curBuff);
        }
    }
    return status;
}

status_t ControlThread::queueCoupledBuffers(int coupledId)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    status = mISP->putRecordingFrame(mCoupledBuffers[coupledId].recordingBuff);
    if (status == NO_ERROR) {
        status = mISP->putPreviewFrame(mCoupledBuffers[coupledId].previewBuff);
        if (status == DEAD_OBJECT) {
            LogDetail("Stale preview buffer returned to ISP");
        } else if (status != NO_ERROR) {
            LogError("Error putting preview frame to ISP");
        }
    } else if (status == DEAD_OBJECT) {
        LogDetail("Stale recording buffer returned to ISP");
    } else {
        LogError("Error putting recording frame to ISP");
    }

    return status;
}

status_t ControlThread::handleMessagePictureDone(MessagePictureDone *msg)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;

    // Return the picture frames back to ISP
    status = mISP->putSnapshot(msg->snapshotBuf, msg->postviewBuf);
    if (status == DEAD_OBJECT) {
        LogDetail("Stale snapshot buffer returned to ISP");
    } else if (status != NO_ERROR) {
        LogError("Error in putting snapshot!");
        return status;
    }

    /*
     * As Android designed this call flow, it seems that when we are called with takePicture,
     * are responsible of stopping the preview, but after the picture is done, CamereService
     * is responsible of starting the preview again. Probably, to allow applications to
     * customize the posting of the taken picture to preview window (I know that this time
     * can be customized in an ordinary camera).
     */
    // Now, stop the ISP too, so we can start it in startPreview
    status = mISP->stop();
    if (status != NO_ERROR) {
        LogError("Error stopping ISP!");
        return status;
    }

    // Stop PictureThread
    status = mPictureThread->requestExitAndWait();
    if (status != NO_ERROR) {
        LogError("Error stopping PictureThread!");
        return status;
    }

    return status;
}

status_t ControlThread::validateParameters(const CameraParameters *params)
{
    /**
     * PREVIEW
     */
    int previewWidth, previewHeight;
    params->getPreviewSize(&previewWidth, &previewHeight);
    if (previewWidth <= 0 || previewHeight <= 0) {
        LogError("bad preview size");
        return BAD_VALUE;
    }

    int minFPS, maxFPS;
    params->getPreviewFpsRange(&minFPS, &maxFPS);
    if(minFPS == maxFPS || minFPS > maxFPS) {
        LogError("invalid fps range [%d,%d]", minFPS, maxFPS);
        return BAD_VALUE;
    }

    /**
     * VIDEO
     */
    int videoWidth, videoHeight;
    params->getPreviewSize(&videoWidth, &videoHeight);
    if (videoWidth <= 0 || videoHeight <= 0) {
        LogError("bad video size");
        return BAD_VALUE;
    }

    /**
     * SNAPSHOT
     */
    int pictureWidth, pictureHeight;
    params->getPreviewSize(&pictureWidth, &pictureHeight);
    if (pictureWidth <= 0 || pictureHeight <= 0) {
        LogError("bad picture size");
        return BAD_VALUE;
    }

    /**
     * MISCELLANEOUS
     */

    // TODO: implement validation for other features not listed above

    return NO_ERROR;
}

status_t ControlThread::processDynamicParameters(const CameraParameters *oldParams,
        const CameraParameters *newParams)
{
    status_t status = NO_ERROR;
    int oldZoom = oldParams->getInt(CameraParameters::KEY_ZOOM);
    int newZoom = newParams->getInt(CameraParameters::KEY_ZOOM);

    if (oldZoom != newZoom)
        status = mISP->setZoom(newZoom);

    return status;
}

status_t ControlThread::processStaticParameters(const CameraParameters *oldParams,
        const CameraParameters *newParams)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    bool previewFormatChanged = false;
    bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;

    int oldWidth, newWidth;
    int oldHeight, newHeight;
    int oldFormat, newFormat;

    // see if preview params have changed
    newParams->getPreviewSize(&newWidth, &newHeight);
    oldParams->getPreviewSize(&oldWidth, &oldHeight);
    newFormat = V4L2Format(newParams->getPreviewFormat());
    oldFormat = V4L2Format(oldParams->getPreviewFormat());
    if (newWidth != oldWidth || newHeight != oldHeight ||
            oldFormat != newFormat) {
        LogDetail("preview size/format is changing: old=%d,%d,%d; new=%d,%d,%d",
                oldWidth, oldHeight, oldFormat, newWidth, newHeight, newFormat);
        previewFormatChanged = true;
    }

    // see if video params have changed
    newParams->getVideoSize(&newWidth, &newHeight);
    oldParams->getVideoSize(&oldWidth, &oldHeight);
    if (newWidth != oldWidth || newHeight != oldHeight) {
        LogDetail("video preview size is changing: old=%d,%d; new=%d,%d",
                oldWidth, oldHeight, newWidth, newHeight);
        previewFormatChanged = true;
    }

    // if preview is running and static params have changed, then we need
    // to stop, reconfigure, and restart the isp and all threads.
    if (previewFormatChanged) {
        switch (mState) {
        case STATE_PREVIEW_VIDEO:
        case STATE_PREVIEW_STILL:
            status = restartPreview(videoMode);
            break;
        case STATE_STOPPED:
            break;
        default:
            LogError("formats can only be changed while in preview or stop states");
            break;
        };
    }

    return status;
}

status_t ControlThread::handleMessageSetParameters(MessageSetParameters *msg)
{
    LOG_FUNCTION
    status_t status = NO_ERROR;
    CameraParameters newParams;
    CameraParameters oldParams = mParameters;
    String8 str_params(msg->params);
    newParams.unflatten(str_params);

    // print all old and new params for comparison (debug)
    LogDetail("----------BEGIN OLD PARAMS----------");
    mParameters.dump();
    LogDetail("----------END OLD PARAMS----------");
    LogDetail("----------BEGIN NEW PARAMS----------");
    newParams.dump();
    LogDetail("----------END NEW PARAMS----------");

    status = validateParameters(&newParams);
    if (status != NO_ERROR)
        goto exit;

    mParameters = newParams;

    // Take care of parameters that need to be set while the ISP is stopped
    status = processStaticParameters(&oldParams, &newParams);
    if (status != NO_ERROR)
        goto exit;

    // Take care of parameters that can be set while ISP is running
    status = processDynamicParameters(&oldParams, &newParams);
    if (status != NO_ERROR)
        goto exit;

    mParameters = newParams;

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
        // let app know if we support zoom in the preview mode indicated
        bool videoMode = isParameterSet(CameraParameters::KEY_RECORDING_HINT) ? true : false;
        AtomISP::Mode mode = videoMode ? AtomISP::MODE_VIDEO : AtomISP::MODE_PREVIEW;
        mISP->getZoomRatios(mode, &mParameters);

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

AtomBuffer* ControlThread::findRecordingBuffer(void *findMe)
{
    // This is a small list, so incremental search is not an issue right now
    for (int i = 0; i < NUM_ATOM_BUFFERS; i++) {
        if (mCoupledBuffers[i].recordingBuff->buff->data == findMe)
            return mCoupledBuffers[i].recordingBuff;
    }
    return NULL;
}

status_t ControlThread::dequeuePreview()
{
    LogEntry2(LOG_TAG, __FUNCTION__);
    AtomBuffer *buff;
    status_t status = NO_ERROR;

    status = mISP->getPreviewFrame(&buff);
    if (status == NO_ERROR) {
        if (mState == STATE_PREVIEW_VIDEO || mState == STATE_RECORDING) {
            mCoupledBuffers[buff->id].previewBuff = buff;
            mCoupledBuffers[buff->id].previewBuffReturned = false;
        }
        status = mPreviewThread->preview(buff);
        if (status != NO_ERROR)
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
        mCoupledBuffers[buff->id].recordingBuff = buff;
        mCoupledBuffers[buff->id].recordingBuffReturned = false;
        // See if recording has started.
        // If it has, process the buffer
        // If it hasn't, return the buffer to the driver
        if (mState == STATE_RECORDING) {
            mCallbacks->videoFrameDone(buff, timestamp);
        } else {
            mCoupledBuffers[buff->id].recordingBuffReturned = true;
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
                if (mISP->dataAvailable())
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
                if (mISP->dataAvailable()) {
                    status = dequeueRecording();
                    if (status == NO_ERROR)
                        status = dequeuePreview();
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
