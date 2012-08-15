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

#ifndef ANDROID_LIBCAMERA_JPEG_COMPRESSOR_H
#define ANDROID_LIBCAMERA_JPEG_COMPRESSOR_H

#include <stdio.h>
#include "AtomCommon.h"
#include <utils/Errors.h>

#ifdef USE_INTEL_JPEG
#include <va/va.h>
#include <va/va_tpi.h>
#include <va/va_android.h>
#endif

namespace android {

class SWJpegEncoder;

class JpegCompressor {
public:
    JpegCompressor();
    ~JpegCompressor();

    struct InputBuffer {
        unsigned char *buf;
        int width;
        int height;
        int format;
        int size;

        void clear()
        {
            buf = NULL;
            width = 0;
            height = 0;
            format = 0;
            size = 0;
        }
    };

    struct OutputBuffer {
        unsigned char *buf;
        int width;
        int height;
        int size;
        int quality;

        void clear()
        {
            buf = NULL;
            width = 0;
            height = 0;
            size = 0;
            quality = 0;
        }
    };

    // Encoder functions
    int encode(const InputBuffer &in, const OutputBuffer &out);

    /*
     * Shared buffers encoding
     * In order to use the shared buffers encoding, the caller must follow this call-flow:
     * - startSharedBuffersEncode
     * - getSharedBuffers
     * - encode (using buffers obtained from getSharedBuffers)
     * - stopSharedBuffersEncode
     */
    status_t startSharedBuffersEncode(void *outBuf, int outSize);
    status_t stopSharedBuffersEncode();
    status_t getSharedBuffers(int width, int height, void** sharedBuffersPtr, int sharedBuffersNum);

private:
    int mJpegSize;

    // For buffer sharing
    char* mVaInputSurfacesPtr[MAX_BURST_BUFFERS];
    int mVaInputSurfacesNum;
    int mVaSurfaceWidth;
    int mVaSurfaceHeight;
    // If the picture dimension is <= the below w x h
    // We should use the software jpeg encoder
    static const int MIN_HW_ENCODING_WIDTH = 640;
    static const int MIN_HW_ENCODING_HEIGHT = 480;

    int swEncode(const InputBuffer &in, const OutputBuffer &out);
    int hwEncode(const InputBuffer &in, const OutputBuffer &out);

#ifdef USE_INTEL_JPEG
    // for hw jpeg encoder
    class WrapperLibVA {
    public:
        WrapperLibVA();
        ~WrapperLibVA();
        int init(void);
        /*
            configure and create one or several surfaces with the width/height
            bufNum: created surface numbers
            useCameraBuf: if true, we will pass camera buffer to libva
                          if false, we will create buffer in the libva
            cameraBuf: if useCameraBuf is true, this is used to pass the buffer
        */
        int configSurface(int width, int height, int bufNum, bool useCameraBuf, void *cameraBuf);
        /*
            destroy context and surfaces
        */
        void destroySurface(void);
        /*
            copy RAW NV12 data to the internal of libva
            pRaw: point to the RAW NV12 data.
                  it can be null if useCameraBuf is true
            useCameraBuf: if true, copy camera buffer data to libva
                          if false, no need to copy
        */
        int getJpegSrcData(void *pRaw, bool useCameraBuf);
        /*
            this function is used to set the jpeg quality
            quality: one value from 0 to 100
        */
        int setJpegQuality(int quality);
        /*
            implement the jpeg encoding
        */
        int doJpegEncoding(void);
        /*
            copy jpeg data from libva to outer
            pdst: the buffer which we will copy jpeg to.
            dstSize: the pdst buffer size
            jpegSize: the real jpeg size
        */
        int getJpegData(void *pdst, int dstSize, int *jpegSize);
        void deInit(void);
    private:
        VADisplay mVaDpy;
        VAConfigID mConfigId;
        VASurfaceID mSurfaceId;
        VAContextID mContextId;
        VABufferID mCodedBuf;
        VAImage mSurfaceImage;
        VABufferID mPicParamBuf;
        VAQMatrixBufferJPEG mQMatrix;
        VABufferID mQMatrixBuf;

        // for using camera buffer mode
        VASurfaceAttributeTPI mSurfaceAttrib; // use it if camera buffer is used
        unsigned int mBuffers; // it's used to store camera buffers address

        // the picture dimensions
        int mPicWidth;
        int mPicHeight;
        int mMaxOutJpegBufSize; // the max JPEG Buffer Size
        // only support NV12
        static const unsigned int mSupportedFormat = VA_RT_FORMAT_YUV420;

        // check the return value for libva functions
        #define CHECK_STATUS(status,func, line)                                    \
                if (status != VA_STATUS_SUCCESS) {                                 \
                    LOGE("@%s, line:%d, call %s failed", __FUNCTION__, line, func);\
                    return -1;                                                     \
                }
        /*
            pbuf: we will get mapped buffer from the libva for RAW NV12 data
        */
        int mapJpegSrcBuffers(void **pbuf);
        /*
            it is used to copy the RAW NV12 data from psrc to the internal
            buffer in the libva
            psrc: RAW NV12 data buffer
            pdst: mapped out buffer from libva
        */
        int copySrcDataToLibVA(void *psrc, void *pdst);
        int unmapJpegSrcBuffers(void);
    };
    class WrapperLibVA mLibVA;
#endif  // USE_INTEL_JPEG

    SWJpegEncoder *mSWEncoder;
};

}; // namespace android

#endif // ANDROID_LIBCAMERA_JPEG_COMPRESSOR_H
