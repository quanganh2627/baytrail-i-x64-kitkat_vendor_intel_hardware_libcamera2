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
#define LOG_TAG "Atom_PreviewThread"

#include "PreviewThread.h"
#include <utils/Log.h>
#include "DebugFrameRate.h"
#include "Callbacks.h"

namespace android {

PreviewThread::PreviewThread(ICallbackPreview *previewDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PreviewThread")
    ,mThreadRunning(false)
    ,mDebugFPS(new DebugFrameRate())
    ,mPreviewDoneCallback(previewDone)
    ,mCallbacks(NULL)
{
}

PreviewThread::~PreviewThread()
{
    mDebugFPS.clear();
}

void PreviewThread::setCallbacks(Callbacks *callbacks)
{
    mCallbacks = callbacks;
}

status_t PreviewThread::preview(AtomBuffer *buff)
{
    Message msg;
    msg.id = MESSAGE_ID_PREVIEW;
    msg.data.preview.buff = buff;
    return mMessageQueue.send(&msg);
}

status_t PreviewThread::handleMessageExit()
{
    status_t status = NO_ERROR;
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return status;
}

status_t PreviewThread::handleMessagePreview(MessagePreview *msg)
{
    status_t status = NO_ERROR;

    // TODO: implement preview code

    mDebugFPS->update(); // update fps counter

    mCallbacks->previewFrameDone(msg->buff);
    mPreviewDoneCallback->previewDone(msg->buff);

    return status;
}

status_t PreviewThread::waitForAndExecuteMessage()
{
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            LOGD("handleMessageExit...\n");
            status = handleMessageExit();
            break;

        case MESSAGE_ID_PREVIEW:
            LOGD("handleMessagePreview %d=%p...\n",
                    msg.data.preview.buff->id,
                    msg.data.preview.buff->buff->data);
            status = handleMessagePreview(&msg.data.preview);
            break;

        default:
            LOGE("invalid message\n");
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool PreviewThread::threadLoop()
{
    LOGD("threadLoop\n");
    status_t status = NO_ERROR;

    // start gathering frame rate stats
    mDebugFPS->run();

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    // stop gathering frame rate stats
    mDebugFPS->requestExitAndWait();

    return false;
}

status_t PreviewThread::requestExitAndWait()
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
