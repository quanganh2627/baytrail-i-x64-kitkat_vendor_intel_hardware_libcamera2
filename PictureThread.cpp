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
#define LOG_TAG "Atom_PictureThread"

#include "PictureThread.h"
#include <utils/Log.h>
#include "Callbacks.h"

namespace android {

PictureThread::PictureThread(ICallbackPicture *pictureDone) :
    Thread(true) // callbacks may call into java
    ,mMessageQueue("PictureThread")
    ,mThreadRunning(false)
    ,mPictureDoneCallback(pictureDone)
    ,mCallbacks(NULL)
{
}

PictureThread::~PictureThread()
{
}

void PictureThread::setCallbacks(Callbacks *callbacks)
{
    mCallbacks = callbacks;
}

status_t PictureThread::encode(AtomBuffer *buff)
{
    Message msg;
    msg.id = MESSAGE_ID_ENCODE;
    msg.data.encode.buff = buff;
    return mMessageQueue.send(&msg);
}

status_t PictureThread::handleMessageExit()
{
    status_t status = NO_ERROR;
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return status;
}

status_t PictureThread::handleMessageEncode(MessageEncode *msg)
{
    status_t status = NO_ERROR;

    // TODO: implement encoding

    mPictureDoneCallback->pictureDone(msg->buff);

    return status;
}

status_t PictureThread::waitForAndExecuteMessage()
{
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            LOGD("handleMessageExit...\n");
            status = handleMessageExit();
            break;

        case MESSAGE_ID_ENCODE:
            LOGD("handleMessageEncode %d=%p...\n",
                    msg.data.encode.buff->id,
                    msg.data.encode.buff->buff->data);
            status = handleMessageEncode(&msg.data.encode);
            break;

        default:
            LOGE("invalid message\n");
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool PictureThread::threadLoop()
{
    LOGD("threadLoop\n");
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return false;
}

status_t PictureThread::requestExitAndWait()
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
