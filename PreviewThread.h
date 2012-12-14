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
#include <utils/Vector.h>
#include <camera.h>
#include <camera/CameraParameters.h>
#include "MessageQueue.h"
#include "AtomCommon.h"

namespace android {

class DebugFrameRate;
class Callbacks;
class CallbacksThread;

#define MAX_NUMBER_PREVIEW_GFX_BUFFERS      10  /*!< Maximum capacity of the vector where we store the
                                                     Gfx Preview Buffers*/

// callback for when Preview thread is done with yuv data
class ICallbackPreview {
public:
    ICallbackPreview() {}
    virtual ~ICallbackPreview() {}
    virtual void previewDone(AtomBuffer *memory) = 0;
};

/**
 * class PreviewThread
 *
 * This class is in charge of configuring the preview window send by the client
 * and render the preview buffers sent by CtrlThread
 */
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
    void getDefaultParameters(CameraParameters *params);
    bool isWindowConfigured();
    status_t preview(AtomBuffer *buff);
    status_t setPreviewWindow(struct preview_stream_ops *window);
    status_t setPreviewConfig(int preview_width, int preview_height, int preview_stride,
                              int preview_format, int bufferCount);
    status_t fetchPreviewBuffers(AtomBuffer ** pvBufs, int *count);
    status_t returnPreviewBuffers();
    status_t flushBuffers();
    status_t enableOverlay(bool set = true);

    // TODO: need methods to configure preview thread
    // TODO: decide if configuration method should send a message

// private types
private:

    // thread message id's
    enum MessageId {

        MESSAGE_ID_EXIT = 0,            // call requestExitAndWait
        MESSAGE_ID_PREVIEW,
        MESSAGE_ID_SET_PREVIEW_WINDOW,
        MESSAGE_ID_SET_PREVIEW_CONFIG,
        MESSAGE_ID_FETCH_PREVIEW_BUFS,
        MESSAGE_ID_RETURN_PREVIEW_BUFS,
        MESSAGE_ID_FLUSH,
        MESSAGE_ID_WINDOW_QUERY,

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

    struct MessageSetPreviewConfig {
        int width;
        int height;
        int stride;
        int format;
        int bufferCount;
    };

    // union of all message data
    union MessageData {

        // MESSAGE_ID_PREVIEW
        MessagePreview preview;

        // MESSAGE_ID_SET_PREVIEW_WINDOW
        MessageSetPreviewWindow setPreviewWindow;

        // MESSAGE_ID_SET_PREVIEW_CONFIG
        MessageSetPreviewConfig setPreviewConfig;
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
    status_t handleMessageFlush();
    status_t handleMessageIsWindowConfigured();

    // main message function
    status_t waitForAndExecuteMessage();

    // inherited from Thread
    virtual bool threadLoop();

// private data
private:

    MessageQueue<Message, MessageId> mMessageQueue;
    bool mThreadRunning;
    ICallbackPreview *mPreviewDoneCallback;

    class PreviewMessageHandler {
    public:
        PreviewMessageHandler(PreviewThread* aThread, ICallbackPreview *previewDone);
        virtual ~PreviewMessageHandler();
        virtual status_t handleSetPreviewWindow(MessageSetPreviewWindow *msg) = 0;
        virtual status_t handleSetPreviewConfig(MessageSetPreviewConfig *msg) = 0;
        virtual status_t handlePreview(MessagePreview *msg) = 0;
        virtual status_t handleFetchPreviewBuffers(void) = 0;
        virtual status_t handleReturnPreviewBuffers(void) = 0;
        virtual status_t fetchPreviewBuffers(AtomBuffer **pvBufs, int *count) = 0;
    protected:
        friend class PreviewThread;
        void allocateLocalPreviewBuf(void);
        void freeLocalPreviewBuf(void);
    protected:
        preview_stream_ops_t *mPreviewWindow;   /*!< struct passed from Service to control the native window */
        PreviewThread       *mPvThread;         /*!< pointer to our dad */
        AtomBuffer          mPreviewBuf;        /*!< Local preview buffer to give to the user */
        Callbacks           *mCallbacks;
        CallbacksThread     *mCallbacksThread;
        ICallbackPreview    *mPreviewDoneCallback;
        int                 mMinUndequeued;     /*!< Minimum number frames
                                                     to keep in window */
        sp<DebugFrameRate>  mDebugFPS;          /*!< reference to the object that keeps
                                                     track of the fps */
        int mPreviewWidth;
        int mPreviewHeight;
        int mPreviewStride;
        int mPreviewFormat;
    };

    class GfxPreviewHandler: public PreviewMessageHandler {
    public:
        GfxPreviewHandler(PreviewThread* aThread, ICallbackPreview *previewDone);
        virtual ~GfxPreviewHandler();

        // PreviewMessageHandler IF
        virtual status_t handleSetPreviewWindow(MessageSetPreviewWindow *msg);
        virtual status_t handleSetPreviewConfig(MessageSetPreviewConfig *msg);
        virtual status_t handlePreview(MessagePreview *msg);
        virtual status_t handleFetchPreviewBuffers(void);
        virtual status_t handleReturnPreviewBuffers(void);
        virtual status_t fetchPreviewBuffers(AtomBuffer **pvBufs, int *count);
    private:
        status_t callPreviewDone(MessagePreview *msg);
        status_t allocateGfxPreviewBuffers(int numberOfBuffers);
        status_t freeGfxPreviewBuffers();
        int getGfxBufferStride();

    private:
        friend class PreviewThread;
        Vector<AtomBuffer>  mPreviewBuffers;    /*!< Vector with the buffers retrieved from window */
        Vector<int>         mPreviewInClient;   /*!< Vector with indexes to mPreviewBuffers*/
        int                 mBuffersInWindow;   /*!< Number of buffers currently in the preview window */

    };

    class OverlayPreviewHandler: public PreviewMessageHandler {
       public:
           OverlayPreviewHandler(PreviewThread* aThread, ICallbackPreview *previewDone);
           virtual ~OverlayPreviewHandler(){};

           // PreviewMessageHandler IF
           virtual status_t handleSetPreviewWindow(MessageSetPreviewWindow *msg);
           virtual status_t handleSetPreviewConfig(MessageSetPreviewConfig *msg);
           virtual status_t handlePreview(MessagePreview *msg);
           virtual status_t handleFetchPreviewBuffers(void);
           virtual status_t handleReturnPreviewBuffers(void);
           virtual status_t fetchPreviewBuffers(AtomBuffer **pvBufs, int *count);
    };

    PreviewMessageHandler   *mMessageHandler;
}; // class PreviewThread

}; // namespace android

#endif // ANDROID_LIBCAMERA_PREVIEW_THREAD_H
