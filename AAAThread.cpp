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
#define LOG_TAG "Atom_AAAThread"

#include "LogHelper.h"
#include "Callbacks.h"
#include "AAAThread.h"
#include "AtomAAA.h"

namespace android {

AAAThread::AAAThread(ICallbackAAA *aaaDone) :
    Thread(false)
    ,mMessageQueue("AAAThread", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mAAA(AtomAAA::getInstance())
    ,mCallbacks(Callbacks::getInstance())
    ,mAAADoneCallback(aaaDone)
    ,m3ARunning(false)
    ,mDVSRunning(false)
    ,mStartAF(false)
    ,mStopAF(false)
{
    LOG1("@%s", __FUNCTION__);
}

AAAThread::~AAAThread()
{
    LOG1("@%s", __FUNCTION__);
}

status_t AAAThread::enable3A()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_ENABLE_AAA;
    return mMessageQueue.send(&msg, MESSAGE_ID_ENABLE_AAA);
}

status_t AAAThread::enableDVS(bool en)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_ENABLE_DVS;
    msg.data.enable.enable = en;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::autoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::cancelAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::newFrame()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_NEW_FRAME;
    status = mMessageQueue.send(&msg);
    return status;
}

status_t AAAThread::applyRedEyeRemoval(AtomBuffer *snapshotBuffer, AtomBuffer *postviewBuffer, int width, int height, int format)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (snapshotBuffer == NULL || snapshotBuffer->buff == NULL)
        return INVALID_OPERATION;

    Message msg;
    msg.id = MESSAGE_ID_REMOVE_REDEYE;
    msg.data.picture.snaphotBuf = *snapshotBuffer;
    msg.data.picture.postviewBuf = *postviewBuffer;
    msg.data.picture.format = format;
    msg.data.picture.height = height;
    msg.data.picture.width = width;
    status = mMessageQueue.send(&msg);
    return status;
}

status_t AAAThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    m3ARunning = false;
    mDVSRunning = false;

    return status;
}

status_t AAAThread::handleMessageEnable3A()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    m3ARunning = true;
    mMessageQueue.reply(MESSAGE_ID_ENABLE_AAA, status);
    return status;
}

status_t AAAThread::handleMessageEnableDVS(MessageEnable* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mDVSRunning = msg->enable;
    return status;
}

status_t AAAThread::handleMessageAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mAAA->is3ASupported()) {
        mAAA->setAfEnabled(true);
        mAAA->setAeLock(true);
        mAAA->setAwbLock(true);
        mAAA->startStillAf();
        mFramesTillAfComplete = 0;
        mStartAF = true;
        mStopAF = false;
    } else {
        mCallbacks->autofocusDone(true);
    }

    return status;
}

status_t AAAThread::handleMessageCancelAutoFocus()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mStartAF) {
        mStopAF = true;
    }

    return status;
}

status_t AAAThread::handleMessageNewFrame()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if (!mDVSRunning && !m3ARunning)
        return status;

    if(m3ARunning){
        // Run 3A statistics
        status = mAAA->apply3AProcess();

        // If auto-focus was requested, run auto-focus sequence
        if (status == NO_ERROR && mStartAF) {
            // Check for cancel-focus
            ci_adv_af_status afStatus = ci_adv_af_status_error;
            if (mStopAF) {
                afStatus = ci_adv_af_status_canceled;
            } else {
                afStatus = mAAA->isStillAfComplete();
                mFramesTillAfComplete++;
            }
            bool stopStillAf = false;
            if (afStatus == ci_adv_af_status_busy) {
                LOG1("StillAF@Frame %d: BUSY    (continuing...)", mFramesTillAfComplete);
            } else if (afStatus == ci_adv_af_status_success) {
                LOG1("StillAF@Frame %d: SUCCESS (stopping...)", mFramesTillAfComplete);
                stopStillAf = true;
            } else if (afStatus == ci_adv_af_status_error) {
                LOG1("StillAF@Frame %d: FAIL    (stopping...)", mFramesTillAfComplete);
                stopStillAf = true;
            } else if (afStatus == ci_adv_af_status_canceled) {
                LOG1("StillAF@Frame %d: CANCEL  (stopping...)", mFramesTillAfComplete);
                stopStillAf = true;
            }

            if (stopStillAf) {
                mAAA->stopStillAf();
                mAAA->setAeLock(false);
                mAAA->setAwbLock(false);
                mAAA->setAfEnabled(false);
                mStartAF = false;
                mStopAF = false;
                mFramesTillAfComplete = 0;
                mCallbacks->autofocusDone(afStatus == ci_adv_af_status_success);
                // Also notify ControlThread that the auto-focus is finished
                mAAADoneCallback->autoFocusDone();
            }
        }
    }

    if(mDVSRunning){
        status = mAAA->applyDvsProcess();
    }
    return status;
}

status_t AAAThread::handleMessageRemoveRedEye(MessagePicture* msg)
{
    status_t status = NO_ERROR;

    status = mAAA->applyRedEyeRemoval(msg->snaphotBuf, msg->width, msg->height, msg->format);

    // When the red-eye removal is done, send back the buffers to ControlThread to encode picture.
    mAAADoneCallback->redEyeRemovalDone(&msg->snaphotBuf, &msg->postviewBuf);
    return status;
}

status_t AAAThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_ENABLE_AAA:
            status = handleMessageEnable3A();
            break;

        case MESSAGE_ID_ENABLE_DVS:
            status = handleMessageEnableDVS(&msg.data.enable);
            break;

        case MESSAGE_ID_AUTO_FOCUS:
            status = handleMessageAutoFocus();
            break;

        case MESSAGE_ID_CANCEL_AUTO_FOCUS:
            status = handleMessageCancelAutoFocus();
            break;

        case MESSAGE_ID_NEW_FRAME:
            status = handleMessageNewFrame();
            break;

    case MESSAGE_ID_REMOVE_REDEYE:
            status = handleMessageRemoveRedEye(&msg.data.picture);
            break;

        default:
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool AAAThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return status == NO_ERROR;
}

status_t AAAThread::requestExitAndWait()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_EXIT;

    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

} // namespace android
