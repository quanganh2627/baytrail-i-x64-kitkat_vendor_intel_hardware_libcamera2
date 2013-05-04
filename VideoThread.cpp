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
#define LOG_TAG "Camera_VideoThread"

#include "VideoThread.h"
#include "LogHelper.h"
#include "CallbacksThread.h"
#include "IntelParameters.h"
#include "PlatformData.h"

namespace android {

VideoThread::VideoThread() :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("VideoThread", MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mCallbacksThread(CallbacksThread::getInstance())
    ,mSlowMotionRate(1)
    ,mFirstFrameTimestamp(0)
{
    LOG1("@%s", __FUNCTION__);
}

VideoThread::~VideoThread()
{
    LOG1("@%s", __FUNCTION__);
}

status_t VideoThread::video(AtomBuffer *buff)
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_VIDEO;
    msg.data.video.buff = *buff;
    return mMessageQueue.send(&msg);
}

status_t VideoThread::flushBuffers()
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_FLUSH;
    mMessageQueue.remove(MESSAGE_ID_VIDEO);
    return mMessageQueue.send(&msg, MESSAGE_ID_FLUSH);
}

status_t VideoThread::setSlowMotionRate(int rate)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_SET_SLOWMOTION_RATE;
    msg.data.setSlowMotionRate.rate = rate;
    return mMessageQueue.send(&msg);
}

void VideoThread::getDefaultParameters(CameraParameters *intel_params, int cameraId)
{
    LOG1("@%s", __FUNCTION__);
    if (!intel_params) {
        LOGE("params is null!");
        return;
    }
    // Set slow motion rate in high speed mode
    if (PlatformData::supportsSlowMotion(cameraId)) {
        intel_params->set(IntelCameraParameters::KEY_SLOW_MOTION_RATE, IntelCameraParameters::SLOW_MOTION_RATE_1X);
        intel_params->set(IntelCameraParameters::KEY_SUPPORTED_SLOW_MOTION_RATE, "1x,2x,3x,4x");
    }
}

status_t VideoThread::handleMessageExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

status_t VideoThread::handleMessageVideo(MessageVideo *msg)
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    nsecs_t timestamp = (msg->buff.capture_timestamp.tv_sec)*1000000000LL
                        + (msg->buff.capture_timestamp.tv_usec)*1000LL;
    if(mSlowMotionRate > 1)
    {
        if(mFirstFrameTimestamp == 0)
            mFirstFrameTimestamp = timestamp;
        timestamp = (timestamp - mFirstFrameTimestamp) * mSlowMotionRate + mFirstFrameTimestamp;
    }
    mCallbacksThread->videoFrameDone(&msg->buff, timestamp);

    return status;
}

status_t VideoThread::handleMessageFlush()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mFirstFrameTimestamp = 0;
    mMessageQueue.reply(MESSAGE_ID_FLUSH, status);
    return status;
}

status_t VideoThread::handleMessageSetSlowMotionRate(MessageSetSlowMotionRate* msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mSlowMotionRate = msg->rate;
    return status;
}

status_t VideoThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            status = handleMessageExit();
            break;

        case MESSAGE_ID_VIDEO:
            status = handleMessageVideo(&msg.data.video);
            break;

        case MESSAGE_ID_FLUSH:
            status = handleMessageFlush();
            break;

        case MESSAGE_ID_SET_SLOWMOTION_RATE:
            status = handleMessageSetSlowMotionRate(&msg.data.setSlowMotionRate);
            break;

        default:
            LOGE("Invalid message");
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool VideoThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return false;
}

status_t VideoThread::requestExitAndWait()
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
