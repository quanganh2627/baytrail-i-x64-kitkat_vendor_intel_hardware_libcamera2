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

#ifndef ANDROID_LIBCAMERA_PREVIEW_THREAD_H
#define ANDROID_LIBCAMERA_PREVIEW_THREAD_H

#include <utils/threads.h>
#include <camera.h>
#include "MessageQueue.h"
#include "AtomCommon.h"

namespace android {

class DebugFrameRate;
class Callbacks;

// callback for when Preview thread is done with yuv data
class ICallbackPreview {
public:
    ICallbackPreview() {}
    virtual ~ICallbackPreview() {}
    virtual void previewDone(AtomBuffer *memory) = 0;
};

class PreviewThread : public Thread {

// constructor destructor
public:
    PreviewThread(ICallbackPreview *previewDone);
    virtual ~PreviewThread();

// Thread overrides
public:
    status_t requestExitAndWait();

// public methods
public:

    void     setCallbacks(Callbacks *callbacks);

    status_t preview(AtomBuffer *buff);
    status_t setPreviewWindow(struct preview_stream_ops *window);
    status_t setPreviewSize(int preview_width, int preview_height);

    // TODO: need methods to configure preview thread
    // TODO: decide if configuration method should send a message

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_PREVIEW,
        MESSAGE_ID_SET_PREVIEW_WINDOW,
        MESSAGE_ID_SET_PREVIEW_SIZE,

        // max number of messages
        MESSAGE_ID_MAX
    };

    //
    // message data structures
    //

    struct MessagePreview {
        AtomBuffer buff;
    };

    struct MessageSetPreviewWindow {
        struct preview_stream_ops *window;
    };

    struct MessageSetPreviewSize {
        int width;
        int height;
    };

    // union of all message data
    union MessageData {

        // MESSAGE_ID_PREVIEW
        MessagePreview preview;

        // MESSAGE_ID_SET_PREVIEW_WINDOW
        MessageSetPreviewWindow setPreviewWindow;

        // MESSAGE_ID_SET_PREVIEW_SIZE
        MessageSetPreviewSize setPreviewSize;
    };

    // message id and message data
    struct Message {
        MessageId id;
        MessageData data;
    };

// private methods
private:

    // thread message execution functions
    status_t handleMessageExit();
    status_t handleMessagePreview(MessagePreview *msg);
    status_t handleMessageSetPreviewWindow(MessageSetPreviewWindow *msg);
    status_t handleMessageSetPreviewSize(MessageSetPreviewSize *msg);

    // main message function
    status_t waitForAndExecuteMessage();

// inherited from Thread
private:
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message> mMessageQueue;
    bool mThreadRunning;
    sp<DebugFrameRate> mDebugFPS;
    ICallbackPreview *mPreviewDoneCallback;
    Callbacks *mCallbacks;

    preview_stream_ops_t* mPreviewWindow;

    int mPreviewWidth;
    int mPreviewHeight;

}; // class PreviewThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_PREVIEW_THREAD_H
