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

#include "AAAThread.h"
#include <utils/Log.h>
#include "AtomAAA.h"

namespace android {

AAAThread::AAAThread() :
    Thread(false)
    ,mMessageQueue("AAAThread")
    ,mThreadRunning(false)
    ,mAAA(new AtomAAA())
{
}

AAAThread::~AAAThread()
{
    delete mAAA;
}

status_t AAAThread::autoFocus()
{
    Message msg;
    msg.id = MESSAGE_ID_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::cancelAutoFocus()
{
    Message msg;
    msg.id = MESSAGE_ID_CANCEL_AUTO_FOCUS;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::runAAA()
{
    Message msg;
    msg.id = MESSAGE_ID_RUN_AAA;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::runDVS()
{
    Message msg;
    msg.id = MESSAGE_ID_RUN_DVS;
    return mMessageQueue.send(&msg);
}

status_t AAAThread::handleMessageExit()
{
    status_t status = NO_ERROR;
    mThreadRunning = false;

    // TODO: any other cleanup that may need to be done

    return status;
}

status_t AAAThread::handleMessageAutoFocus()
{
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t AAAThread::handleMessageCancelAutoFocus()
{
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t AAAThread::handleMessageRunAAA()
{
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t AAAThread::handleMessageRunDVS()
{
    status_t status = NO_ERROR;

    // TODO: implement

    return status;
}

status_t AAAThread::waitForAndExecuteMessage()
{
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id) {

        case MESSAGE_ID_EXIT:
            LOGD("handleMessageExit...\n");
            status = handleMessageExit();
            break;

        case MESSAGE_ID_AUTO_FOCUS:
            LOGD("handleMessageAutoFocus...\n");
            status = handleMessageAutoFocus();
            break;

        case MESSAGE_ID_CANCEL_AUTO_FOCUS:
            LOGD("handleMessageCancelAutoFocus...\n");
            status = handleMessageCancelAutoFocus();
            break;

        case MESSAGE_ID_RUN_AAA:
            LOGD("handleMessageRunAAA...\n");
            status = handleMessageRunAAA();
            break;

        case MESSAGE_ID_RUN_DVS:
            LOGD("handleMessageRunDVS...\n");
            status = handleMessageRunDVS();
            break;

        default:
            LOGE("invalid message\n");
            status = BAD_VALUE;
            break;
    };
    return status;
}

bool AAAThread::threadLoop()
{
    LOGD("threadLoop\n");
    status_t status = NO_ERROR;

    mThreadRunning = true;
    while (mThreadRunning)
        status = waitForAndExecuteMessage();

    return status == NO_ERROR;
}

status_t AAAThread::requestExitAndWait()
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
