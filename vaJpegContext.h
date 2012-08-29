/*
 * Copyright (C) 2012 The Android Open Source Project
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

#ifndef VAJPEGCONTEXT_H_
#define VAJPEGCONTEXT_H_

#include <va/va.h>
#include <va/va_tpi.h>
#include <va/va_android.h>
#include <utils/KeyedVector.h>


/**
 * \define CHECK_STATUS
 * \brief  check the return value for libva functions
 */
#define CHECK_STATUS(status,func, line)                                    \
        if (status != VA_STATUS_SUCCESS) {                                 \
            LOGE("@%s, line:%d, call %s failed", __FUNCTION__, line, func);\
            return -1;                                                     \
        }
/**
 * \define ERROR_POINTER_NOT_FOUND
 * \brief default value used to detect that a buffer address does not have an
 *  assigned VA Surface
 */
#define ERROR_POINTER_NOT_FOUND 0xDEADBEEF
namespace android {


/**
 * \struct vaJpegContext
 *
 * Contains all the VA specific types used by the JpegHWEncoder
 * class.
 * In this way we isolate libVA types from users of the
 * JpegHwEncoder class
 */
struct vaJpegContext {
    vaJpegContext():mBuff2SurfId(ERROR_POINTER_NOT_FOUND) {
        memset(&mSurfaceImage, 0, sizeof(mSurfaceImage));
        memset(&mQMatrix, 0, sizeof(mQMatrix));
        mBuff2SurfId.setCapacity(MAX_BURST_BUFFERS);
        mDpy = 0;
        mConfigId = 0;
        mContextId = 0;
        mCodedBuf = 0;
        mQMatrixBuf = 0;
        mCodedBufList = NULL;
        mPicParamBuf = 0;
        mCodedBufList = NULL;
        mCurrentSurface = 0;
    };
    VADisplay mDpy;
    VAConfigID mConfigId;

    VAContextID mContextId;
    VABufferID mCodedBuf;
    VABufferID mQMatrixBuf;
    VABufferID mPicParamBuf;
    VAQMatrixBufferJPEG mQMatrix;
    VACodedBufferSegment *mCodedBufList;

    VAImage mSurfaceImage;
    VASurfaceID mSurfaceIds[MAX_BURST_BUFFERS];
    DefaultKeyedVector<unsigned int, VASurfaceID> mBuff2SurfId;
    VASurfaceID mCurrentSurface;
    // only support NV12
    static const unsigned int mSupportedFormat = VA_RT_FORMAT_YUV420;
};

};

#endif /* VAJPEGCONTEXT_H_ */
