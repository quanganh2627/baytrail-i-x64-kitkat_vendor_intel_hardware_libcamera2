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
#include <utils/Log.h>
#include "PreviewThread.h"
#include "PictureThread.h"
#include "AtomISP.h"
#include "Callbacks.h"

namespace android {

ControlThread::ControlThread() :
    Thread(true) // callbacks may call into java
    ,mISP(new AtomISP())
    ,mPreviewThread(new PreviewThread((ICallbackPreview *) this))
    ,mPictureThread(new PictureThread((ICallbackPicture *) this))
    ,mMessageQueue("ControlThread", (int) MESSAGE_ID_MAX)
    ,mState(STATE_STOPPED)
    ,mThreadRunning(false)
    ,mCallbacks(new Callbacks())
    ,mNumPreviewFramesOut(0)
    ,mNumRecordingFramesOut(0)
{
}

ControlThread::~ControlThread()
{
    mPreviewThread.clear();
    mPictureThread.clear();
    delete mISP;
    delete mCallbacks;
}

status_t ControlThread::setPreviewWindow(struct preview_stream_ops *window)
{
    // TODO: implement (probably pass to PreviewThread)
    return NO_ERROR;
}

void ControlThread::setCallbacks(camera_notify_callback notify_cb,
                                 camera_data_callback data_cb,
                                 camera_data_timestamp_callback data_cb_timestamp,
                                 camera_request_memory get_memory,
                                 void* user)
{
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
    mCallbacks->enableMsgType(msgType);
}

void ControlThread::disableMsgType(int32_t msgType)
{
    mCallbacks->disableMsgType(msgType);
}

bool ControlThread::msgTypeEnabled(int32_t msgType)
{
    return mCallbacks->msgTypeEnabled(msgType);
}

status_t ControlThread::startPreview()
{
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_START_PREVIEW;
    return mMessageQueue.send(&msg, MESSAGE_ID_START_PREVIEW);
}

status_t ControlThread::stopPreview()
{
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_STOP_PREVIEW;
    return mMessageQueue.send(&msg, MESSAGE_ID_STOP_PREVIEW);
}

status_t ControlThread::startRecording()
{
    // send message and block until thread processes message
    Message msg;
    msg.id = MESSAGE_ID_START_RECORDING;
    return mMessageQueue.send(&msg, MESSAGE_ID_START_RECORDING);
}

status_t ControlThread::stopRecording()
{
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
    // TODO: implement (decide if this should be a message for thread safe code)
    return NO_ERROR;
}

char *ControlThread::getParameters()
{
    return NULL; // TODO: implement (allocate memory)
}

void ControlThread::putParameters(char *params)
{
    // TODO: implement (and free memory)
}

status_t ControlThread::takePicture()
{
    Message msg;
    msg.id = MESSAGE_ID_TAKE_PICTURE;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::cancelPicture()
{
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_PICTURE;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::autoFocus()
{
    Message msg;
    msg.id = MESSAGE_ID_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::cancelAutoFocus()
{
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t ControlThread::releaseRecordingFrame(void *buff)
{
    Message msg;
    msg.id = MESSAGE_ID_RELEASE_RECORDING_FRAME;
    msg.data.releaseRecordingFrame.buff = buff;
    return mMessageQueue.send(&msg);
}

void ControlThread::previewDone(AtomBuffer *buff)
{
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW_DONE;
    msg.data.previewDone.buff = buff;
    mMessageQueue.send(&msg);
}

void ControlThread::pictureDone(AtomBuffer *buff)
{
    Message msg;
    msg.id = MESSAGE_ID_PICTURE_DONE;
    msg.data.pictureDone.buff = buff;
    mMessageQueue.send(&msg);
}

status_t ControlThread::handleMessageExit()
{
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return NO_ERROR;
}

status_t ControlThread::handleMessageStartPreview()
{
    status_t status;
    if (mState == STATE_STOPPED) {
        status = mPreviewThread->run();
        if (status == NO_ERROR) {
            status = mISP->start(AtomISP::MODE_PREVIEW_VIDEO);
            if (status == NO_ERROR) {
                mState = STATE_PREVIEW_VIDEO;
                mNumPreviewFramesOut = 0;
            } else {
                LOGE("error starting isp\n");
            }
        } else {
            LOGE("error starting preview thread\n");
        }
    } else {
        LOGE("error starting preview. invalid state\n");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_START_PREVIEW, status);
    return status;
}

status_t ControlThread::handleMessageStopPreview()
{
    status_t status;
    if (mState != STATE_STOPPED) {
        status = mPreviewThread->requestExitAndWait();
        if (status == NO_ERROR) {
            status = mISP->stop();
            if (status == NO_ERROR) {
                mState = STATE_STOPPED;
            } else {
                LOGE("error stopping isp\n");
            }
        } else {
            LOGE("error stopping preview thread\n");
        }
    } else {
        LOGE("error stopping preview. invalid state\n");
        status = INVALID_OPERATION;
    }


    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_STOP_PREVIEW, status);
    return status;
}

status_t ControlThread::handleMessageStartRecording()
{
    status_t status = NO_ERROR;

    if (mState == STATE_PREVIEW_VIDEO) {
        mState = STATE_RECORDING;
        mNumRecordingFramesOut = 0;
    } else {
        LOGE("error starting recording. invalid state\n");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_START_RECORDING, status);
    return status;
}

status_t ControlThread::handleMessageStopRecording()
{
    status_t status = NO_ERROR;

    if (mState == STATE_RECORDING) {
        mState = STATE_PREVIEW_VIDEO;
    } else {
        LOGE("error stopping recording. invalid state\n");
        status = INVALID_OPERATION;
    }

    // return status and unblock message sender
    mMessageQueue.reply(MESSAGE_ID_STOP_RECORDING, status);
    return status;
}

status_t ControlThread::handleMessageTakePicture()
{
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t ControlThread::handleMessageCancelPicture()
{
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t ControlThread::handleMessageAutoFocus()
{
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t ControlThread::handleMessageCancelAutoFocus()
{
    status_t status = NO_ERROR;
    void *buff;

    // TODO: implement

    return status;
}

status_t ControlThread::handleMessageReleaseRecordingFrame(MessageReleaseRecordingFrame *msg)
{
    status_t status = mISP->putRecordingFrame(msg->buff);
    if (status == NO_ERROR)
        mNumRecordingFramesOut--;
    else
        LOGE("error putting recording frame to isp\n");
    return status;
}

status_t ControlThread::handleMessagePreviewDone(MessagePreviewDone *msg)
{
    status_t status = mISP->putPreviewFrame(msg->buff);
    if (status == NO_ERROR)
        mNumPreviewFramesOut--;
    else
        LOGE("error putting preview frame to isp\n");
    return status;
}

status_t ControlThread::handleMessagePictureDone(MessagePictureDone *msg)
{
    // TODO: implement
    return NO_ERROR;
}

status_t ControlThread::waitForAndExecuteMessage()
{
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            LOGD("handleMessageExit...\n");
            status = handleMessageExit();
            break;

        case MESSAGE_ID_START_PREVIEW:
            LOGD("handleMessageStartPreview...\n");
            status = handleMessageStartPreview();
            break;

        case MESSAGE_ID_STOP_PREVIEW:
            LOGD("handleMessageStopPreview...\n");
            status = handleMessageStopPreview();
            break;

        case MESSAGE_ID_START_RECORDING:
            LOGD("handleMessageStartRecording...\n");
            status = handleMessageStartRecording();
            break;

        case MESSAGE_ID_STOP_RECORDING:
            LOGD("handleMessageStopRecording...\n");
            status = handleMessageStopRecording();
            break;

        case MESSAGE_ID_TAKE_PICTURE:
            LOGD("handleMessageTakePicture...\n");
            status = handleMessageTakePicture();
            break;

        case MESSAGE_ID_CANCEL_PICTURE:
            LOGD("handleMessageCancelPicture...\n");
            status = handleMessageCancelPicture();
            break;

        case MESSAGE_ID_AUTO_FOCUS:
            LOGD("handleMessageAutoFocus...\n");
            status = handleMessageAutoFocus();
            break;

        case MESSAGE_ID_CANCEL_AUTO_FOCUS:
            LOGD("handleMessageCancelAutoFocus...\n");
            status = handleMessageCancelAutoFocus();
            break;

        case MESSAGE_ID_RELEASE_RECORDING_FRAME:
            LOGD("handleMessageReleaseRecordingFrame %p...\n",
                    msg.data.releaseRecordingFrame.buff);
            status = handleMessageReleaseRecordingFrame(&msg.data.releaseRecordingFrame);
            break;

        case MESSAGE_ID_PREVIEW_DONE:
            LOGD("handleMessagePreviewDone %d=%p...\n",
                    msg.data.previewDone.buff->id,
                    msg.data.previewDone.buff->buff->data);
            status = handleMessagePreviewDone(&msg.data.previewDone);
            break;

        case MESSAGE_ID_PICTURE_DONE:
            LOGD("handleMessagePictureDone %d=%p...\n",
                    msg.data.previewDone.buff->id,
                    msg.data.pictureDone.buff->buff->data);
            status = handleMessagePictureDone(&msg.data.pictureDone);
            break;

        default:
            LOGE("invalid message\n");
            status = BAD_VALUE;
            break;
    };

    if (status != NO_ERROR)
        LOGE("error handling message %d\n", (int) msg.id);
    return status;
}

status_t ControlThread::dequeuePreview()
{
    AtomBuffer *buff;
    status_t status = NO_ERROR;

    if (mNumPreviewFramesOut < ATOM_PREVIEW_BUFFERS) {
        status = mISP->getPreviewFrame(&buff);
        if (status == NO_ERROR) {
            status = mPreviewThread->preview(buff);
            if (status == NO_ERROR)
                mNumPreviewFramesOut++;
            else
                LOGE("error sending buffer to preview thread\n");
        } else {
            LOGE("error: gettting recording from isp\n");
        }
    } else {
        // All buffers are in the upper layer somewhere, isp driver
        // doesn't have any. If we try to dequeue buffers from driver
        // we will hang forever. We need to wait until frames returned
        // so we can give them back to the driver.
        LOGW("warning: no buffers in isp\n");
        status = NOT_ENOUGH_DATA;
    }
    return status;
}

status_t ControlThread::dequeueRecording()
{
    AtomBuffer *buff;
    nsecs_t timestamp;
    status_t status = NO_ERROR;

    if (mNumRecordingFramesOut < ATOM_RECORDING_BUFFERS) {
        status = mISP->getRecordingFrame(&buff, &timestamp);
        if (status == NO_ERROR) {
            mCallbacks->videoFrameDone(buff, timestamp);
            mNumRecordingFramesOut++;
        } else {
            LOGE("error: getting recording from isp\n");
        }
    } else {
        // All buffers are in the upper layer somewhere, isp driver
        // doesn't have any. If we try to dequeue buffers from driver
        // we will hang forever. We need to wait until frames returned
        // so we can give them back to the driver.
        LOGW("warning: no buffers in isp\n");
        status = NOT_ENOUGH_DATA;
    }
    return status;
}

bool ControlThread::threadLoop()
{
    LOGD("threadLoop\n");
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning) {

        switch (mState) {

        case STATE_STOPPED:
            // in the stop state all we do is wait for messages
            status = waitForAndExecuteMessage();
            break;

        case STATE_PREVIEW_STILL:
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                status = dequeuePreview();
                if (status == NOT_ENOUGH_DATA) {
                    status = waitForAndExecuteMessage();
                }
            }
            break;

        case STATE_PREVIEW_VIDEO:
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                status = dequeuePreview();
                if (status == NOT_ENOUGH_DATA) {
                    status = waitForAndExecuteMessage();
                }
            }
            break;

        case STATE_RECORDING:
            // message queue always has priority over getting data from the
            // isp driver no matter what state we are in
            if (!mMessageQueue.isEmpty()) {
                status = waitForAndExecuteMessage();
            } else {
                status = dequeuePreview();
                if (status == NO_ERROR) {
                    status = dequeueRecording();
                    if (status == NOT_ENOUGH_DATA) {
                        status = waitForAndExecuteMessage();
                    }
                } else if (status == NOT_ENOUGH_DATA) {
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
    LOGD("requestExit...\n");
    Message msg;
    msg.id = MESSAGE_ID_EXIT;

    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

} // namespace android
