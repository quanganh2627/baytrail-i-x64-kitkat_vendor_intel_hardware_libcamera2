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

#include "OlaService/HalProxyOla.h"
#include "OlaService/OlaBufferService.h"
#include "ControlThread.h"

#define OLABUFFER_STATUS_BEFORECOPY (0)
#define OLABUFFER_STATUS_DIDCOPY    (1)
#define OLABUFFER_STATUS_DIDPROCESS (2)

typedef struct {
    int progressStatus;
    int previewWidth;
    int previewHeight;
} OlaBufferInfo;

namespace android {
static char* g_imageBuffer;

HalProxyOla::HalProxyOla(ControlThread *aControlThread) {

    LOG1("@%s: ", __func__);
    mHAL = aControlThread;
    Ola_BufferService_Initiate();
    g_imageBuffer = Ola_BufferService_GetBufferMemPointer();
    gHAL = this;
    LOG1("@%s: got share mem %p", __func__, g_imageBuffer);

}

HalProxyOla::~HalProxyOla() {

    Ola_BufferService_DeInitialize();
    gHAL = NULL;
}

void HalProxyOla::copyPreview(void* src, int width, int height) {
    LOG1("%s: g_imageBuffer = %p", __FUNCTION__, g_imageBuffer);
    if( g_imageBuffer ) {
        LOG1("%s: frame ptr %p dimension (%dx%d)", __func__, src, width, height);
        OlaBufferInfo * info = (OlaBufferInfo*) g_imageBuffer;
        char* pData = (char*)g_imageBuffer + sizeof(OlaBufferInfo);
        if( info->progressStatus == OLABUFFER_STATUS_DIDPROCESS || info->progressStatus == 0 ) {
            LOG1("copying now");
            int previewSize = width * height * 3 /2;
            info->previewWidth = width;
            info->previewHeight = height;
            memcpy( pData , src, previewSize);
            info->progressStatus = OLABUFFER_STATUS_DIDCOPY;
        }
    } else {
        LOGD("[%s] getting OlaBuffer", __FUNCTION__);
        g_imageBuffer = Ola_BufferService_GetBufferMemPointer();
        LOG1("%s: got ptr %p ", __FUNCTION__, g_imageBuffer);
    }
}
}// namespace android
