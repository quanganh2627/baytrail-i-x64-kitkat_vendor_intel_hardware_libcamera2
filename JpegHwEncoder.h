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

    /**
     * Initialize the HW encoder
     *
     * Initializes the libVA library
     * \return 0 success
     * \return -1 failure
     */
    int init(void);

    /**
     * deInit the HW encoder
     *
     * de-initializes the libVA library
     */
    void deInit(void);

    /**
     * Returns the status of the HW encoder
     *
     * \return true if libVA is initialized
     * \return false if not.
     */
    bool isInitialized() {return mHWInitialized;};

    /**
     *  Configure pre-allocated input buffer
     *
     *  Prepares the encoder to use a set of pre-allocated input buffers
     *  if an encode command comes with a pointer belonging to this set
     *  the encoding process is faster.
     */
    int setInputBuffers(AtomBuffer* inputBuffersArray, int inputBuffersNum);

    /**
     *  Set the JPEG Q factor
     *
     * This function is used to set the jpeg quality
     *
     * \param quality: one value from 0 to 100
     */
    int setJpegQuality(int quality);

    /**
     * Encodes the input buffer placing the  resulting JPEG bitstream in the
     * output buffer. The encoding operation is synchronous
     *
     * \param in: input buffer description
     * \param out: output param description
     * \return 0 if encoding was successful
     * \return -1 if encoding failed
     */
    int encode(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out);

    /**
     * Starts the HW encoding process.
     * After it returns the JPEG is not encoded yet
     * The following steps are:
     * - waitToComplete()
     * - getOutput()
     *
     * \param in  [in]: input buffer descriptor structure
     * \param out [in]: output buffer descriptor. It contains details like
     *                  quality and buffer size
     */
    int encodeAsync(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out);

    /**
     *  Wait for the HW to finish encoding
     *
     *  Part of the asynchronous encoding process.
     *  This call has to be issued after a encodeAsync()
     *  After this call returns the jpeg encoding is complete and the jpeg
     *  bitstream is ready to be retrieved.
     *  At this point usually the destination buffer is allocated with the
     *  correct size
     *
     *  \param jpegSize [out] pointer to an allocated int where the size of the
     *                  encoded jpeg will be stored
     */
    int waitToComplete(int *jpegSize);

    /**
     *  Retrieve the encoded bitstream
     *
     *  Part of the asynchronous encoding process.
     *  Copies the jpeg bistream from the internal buffer allocated by libVA
     *  to the one provided inside the outputBuffer struct
     *
     *  \param out [in] buffer descriptor for the output of the encoding process
     */
    int getOutput(JpegCompressor::OutputBuffer &out);


private:

    int configSurfaces(AtomBuffer* inputBuffersArray, int inputBuffersNum);
    void destroySurfaces(void);
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
    bool            mContextRestoreNeeded;
    int             mVaInputSurfacesNum;
    unsigned int    mBuffers[MAX_BURST_BUFFERS]; // it's used to store camera buffers addresses

    int mPicWidth;          /*!< Input frame width  */
    int mPicHeight;         /*!< Input frame height */
    int mMaxOutJpegBufSize; /*!< the max JPEG Buffer Size. This is initialized to
                                 the size of the input YUV buffer*/
};

}; // namespace android

#endif /* JPEGHWENCODER_H_ */
