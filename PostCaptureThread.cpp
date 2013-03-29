/*
 * Copyright (C) 2013 The Android Open Source Project
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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
#define LOG_TAG "Camera_PostProcThread"
//#define LOG_NDEBUG 0

#include "LogHelper.h"
#include "PostCaptureThread.h"

namespace android {


PostCaptureThread::PostCaptureThread(IPostCaptureProcessObserver *anObserver):
    Thread(false) // callbacks will not call into java
    ,mMessageQueue("PostCaptureThread", (int) MESSAGE_ID_MAX)
    ,mThreadRunning(false)
    ,mObserver(anObserver)
{

}

PostCaptureThread::~PostCaptureThread()
{

}

status_t PostCaptureThread::handleExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

status_t PostCaptureThread::sendProcessItem(IPostCaptureProcessItem* item)
{
    LOG1("@%s", __FUNCTION__);
    Message msg;

    msg.id = MESSAGE_ID_PROCESS_ITEM;
    msg.data.procItem.item = item;

    return mMessageQueue.send(&msg);

}

status_t PostCaptureThread::handleProcessItem(MessageProcessItem &msg)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    IPostCaptureProcessItem *processAlgo = msg.item;

    status = processAlgo->process();

    mObserver->postCaptureProcesssingDone(processAlgo, status);

    return status;
}

status_t PostCaptureThread::requestExitAndWait()
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_EXIT;
    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

status_t PostCaptureThread::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id)
    {
        case MESSAGE_ID_PROCESS_ITEM:
            status = handleProcessItem(msg.data.procItem);
            break;
        case MESSAGE_ID_EXIT:
            status = handleExit();
            break;
        default:
            status = INVALID_OPERATION;
            break;
    }
    if (status != NO_ERROR) {
        LOGE("operation failed, ID = %d, status = %d", msg.id, status);
    }
    return status;
}

bool PostCaptureThread::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    mThreadRunning = true;
    while(mThreadRunning)
        waitForAndExecuteMessage();

    return false;
}
} // namespace android
