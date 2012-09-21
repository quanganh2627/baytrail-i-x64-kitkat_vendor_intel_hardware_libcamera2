/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
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

/**
 *\file JpegHwEncoder.h
 *
 * Abstracts the HW accelerated JPEG encoder
 *
 * It allow synchronous and asynchronous encoding
 * It interfaces with libVA to access the HW that encoder in JPEG
 * All libVA API are stored inside the vaJpegContext struct to confine the
 * libVA types as an implementation detail of this class
 *
 * The JpegCompressor class is the main user of this class
 *
 */

#ifndef JPEGHWENCODER_H_
#define JPEGHWENCODER_H_

#include "JpegCompressor.h"

namespace android {

// Forward declarations
struct vaJpegContext;

/**
 * \define
 * size of the JPEG markers (SOI or EOI) in bytes
 */
#define SIZE_OF_JPEG_MARKER 2

#ifdef USE_INTEL_JPEG
/**
 * \class JpegHwEncoder
 *
 * This class offers HW accelerated JPEG encoding.
 * Since actual encoding is done in HW it offers synchronous and
 * asynchronous interfaces to the encoding
 */
class JpegHwEncoder {

public:

    JpegHwEncoder();
    virtual ~JpegHwEncoder();

    int init(void);
    int deInit(void);
    bool isInitialized() {return mHWInitialized;};
    int setInputBuffers(AtomBuffer* inputBuffersArray, int inputBuffersNum);
    int setJpegQuality(int quality);
    int encode(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out);
    /* Async encode */
    int encodeAsync(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out);
    int waitToComplete(int *jpegSize);
    int getOutput(JpegCompressor::OutputBuffer &out);

// prevent copy constructor and assignment operator
private:
    JpegHwEncoder(const JpegHwEncoder& other);
    JpegHwEncoder& operator=(const JpegHwEncoder& other);

private:

    int configSurfaces(AtomBuffer* inputBuffersArray, int inputBuffersNum);
    int destroySurfaces(void);
    int startJpegEncoding(unsigned int aSurface);
    int getJpegData(void *pdst, int dstSize, int *jpegSize);
    int getJpegSize(int *jpegSize);
    int resetContext(const JpegCompressor::InputBuffer &in, unsigned int* aSurface);
    int restoreContext();
private:
    // If the picture dimension is <= the below w x h
    // We should use the software jpeg encoder
    static const int MIN_HW_ENCODING_WIDTH = 640;
    static const int MIN_HW_ENCODING_HEIGHT = 480;

    vaJpegContext*  mVaEncoderContext;
    bool            mHWInitialized;
    bool            mContextRestoreNeeded;       /*!< flags the need for a libVA context restore */
    int             mVaInputSurfacesNum;         /*!< number of input surface created from buffers
                                                      allocated by PictureThread */
    unsigned int    mBuffers[MAX_BURST_BUFFERS]; /*!< it's used to store camera buffers addresses*/

    int mPicWidth;          /*!< Input frame width  */
    int mPicHeight;         /*!< Input frame height */
    int mMaxOutJpegBufSize; /*!< the max JPEG Buffer Size. This is initialized to
                                 the size of the input YUV buffer*/
};
#else  //USE_INTEL_JPEG
//Stub implementation if HW encoder is disabled

class JpegHwEncoder {

public:
    JpegHwEncoder(){};
    virtual ~JpegHwEncoder(){};

    int init(void){return -1;};
    int deInit(void){return -1;};
    bool isInitialized() {return false;};
    int setInputBuffers(AtomBuffer* inputBuffersArray, int inputBuffersNum){return -1;};
    int encode(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out){return -1;};
    int encodeAsync(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out){return -1;};
    int waitToComplete(int *jpegSize){return -1;};
    int getOutput(JpegCompressor::OutputBuffer &out){return -1;};

// prevent copy constructor and assignment operator
private:
    JpegHwEncoder(const JpegHwEncoder& other);
    JpegHwEncoder& operator=(const JpegHwEncoder& other);
};
#endif
}; // namespace android

#endif /* JPEGHWENCODER_H_ */
