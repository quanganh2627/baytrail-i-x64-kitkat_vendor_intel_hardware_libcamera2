/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
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
#define LOG_TAG "Camera_JpegHwEncoder"

#include "JpegCompressor.h"
#include "LogHelper.h"
#include "JpegHwEncoder.h"
#include "vaJpegContext.h"

namespace android {

JpegHwEncoder::JpegHwEncoder() :
    mVaEncoderContext(NULL),
    mHWInitialized(false),
    mContextRestoreNeeded(false),
    mVaInputSurfacesNum(0),
    mPicWidth(0),
    mPicHeight(0),
    mMaxOutJpegBufSize(0)
{
    LOG1("@%s", __FUNCTION__);
    mVaEncoderContext = new vaJpegContext();

}

JpegHwEncoder::~JpegHwEncoder()
{
    LOG1("@%s", __FUNCTION__);
    if(mHWInitialized)
        deInit();
    if(mVaEncoderContext != NULL)
        delete mVaEncoderContext;
}

/**
 * Initialize the HW encoder
 *
 * Initializes the libVA library.
 * It could fail in cases where the video HW encoder is already initialized.
 * This is handled normally by the PictureThread falls back to the SW encoder
 *
 * \return 0 success
 * \return -1 failure
 */
int JpegHwEncoder::init(void)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    int display_num, major_ver, minor_ver;
    int num_entrypoints, i, maxNum;
    const char *driver = NULL;
    VAEntrypoint entrypoints[VAEntrypointMax];
    VAConfigAttrib attrib;
    vaJpegContext *va;

    if (mVaEncoderContext == NULL) {
        LOGE("Failed to create VA encoder context struct, no memory");
        mHWInitialized = false;
        return -1;
    }

    va = mVaEncoderContext;
    va->mDpy = vaGetDisplay(&display_num);
    status = vaInitialize(va->mDpy, &major_ver, &minor_ver);
    CHECK_STATUS(status, "vaInitialize", __LINE__)

    driver = vaQueryVendorString(va->mDpy);
    maxNum = vaMaxNumEntrypoints(va->mDpy);
    status = vaQueryConfigEntrypoints(va->mDpy, VAProfileJPEGBaseline,
                                      entrypoints, &num_entrypoints);
    CHECK_STATUS(status, "vaQueryConfigEntrypoints", __LINE__)

    for(i = 0; i < num_entrypoints; i++) {
        if (entrypoints[i] == VAEntrypointEncPicture)
            break;
    }
    if (i == num_entrypoints) {
        LOGE("@%s, line:%d, not find Slice entry point, num:%d",
                __FUNCTION__, __LINE__, num_entrypoints);
        return -1;
    }

    attrib.type = VAConfigAttribRTFormat;
    attrib.value = va->mSupportedFormat;
    status = vaCreateConfig(va->mDpy, VAProfileJPEGBaseline, VAEntrypointEncPicture,
                            &attrib, 1, &va->mConfigId);
    if (status != VA_STATUS_SUCCESS) {
       vaTerminate(va->mDpy);
    }
    CHECK_STATUS(status, "vaCreateConfig", __LINE__)

    mHWInitialized = true;
    return 0;
}

/**
 * deInit the HW encoder
 *
 * de-initializes the libVA library
 */
int JpegHwEncoder::deInit()
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    vaJpegContext *va = mVaEncoderContext;

    if(va->mBuff2SurfId.size() != 0)
        destroySurfaces();

    if (va->mDpy && va->mConfigId) {
        status = vaDestroyConfig(va->mDpy, va->mConfigId);
        CHECK_STATUS(status, "vaDestroyConfig", __LINE__)
    }
    if (va->mDpy) {
        status = vaTerminate(va->mDpy);
        CHECK_STATUS(status, "vaTerminate", __LINE__)
    }
    va->mDpy = 0;
    va->mConfigId = 0;
    mHWInitialized = false;
    return 0;
}

/**
 *  Configure pre-allocated input buffer
 *
 *  Prepares the encoder to use a set of pre-allocated input buffers
 *  if an encode command comes with a pointer belonging to this set
 *  the encoding process is faster.
 */
status_t JpegHwEncoder::setInputBuffers(AtomBuffer* inputBuffersArray, int inputBuffersNum)
{
    LOG1("@%s", __FUNCTION__);

    if(isInitialized())
       deInit();

    /**
     * if we want to create and destroy the libVA context for each capture we may be
     * configured like this. This happens in video mode where the video encoder
     * context also needs to exist
     */
    if(inputBuffersNum == 0) {
        LOG1("HW encoder configured with 0 pre-allocated buffers");
        mVaInputSurfacesNum = 0;
        return NO_ERROR;
    }

    if (init() < 0) {
        LOGE("HW encoder failed to initialize when setting the input buffers");
        return -1;
    }

    if(configSurfaces(inputBuffersArray, inputBuffersNum) < 0) {
        LOGE("HW encoder coudl  not create the libVA context");
        return -1;
    }

    return 0;
}

/**
 * Encodes the input buffer placing the  resulting JPEG bitstream in the
 * output buffer. The encoding operation is synchronous
 *
 * \param in: input buffer description
 * \param out: output param description
 * \return 0 if encoding was successful
 * \return -1 if encoding failed
 */
int JpegHwEncoder::encode(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out)
{
    LOG1("@%s", __FUNCTION__);
    int status;
    VASurfaceID aSurface = 0;
    VAEncPictureParameterBufferJPEG pic_jpeg;
    vaJpegContext *va = mVaEncoderContext;

    if ((in.width <= MIN_HW_ENCODING_WIDTH && in.height <= MIN_HW_ENCODING_HEIGHT)
       || in.format != V4L2_PIX_FMT_NV12) {
         LOG1("@%s, line:%d, do not use the hw jpeg encoder", __FUNCTION__, __LINE__);
         return -1;
     }

    LOG1("input buffer address: %p", in.buf);

    aSurface = va->mBuff2SurfId.valueFor((unsigned int)in.buf);
    if(aSurface == ERROR_POINTER_NOT_FOUND) {
        LOGW("Received buffer does not map to any surface");
        status = resetContext(in, &aSurface);
        mContextRestoreNeeded = true;
        if(status) {
            LOGE("Encoder failed to reset the libVA context");
            return -1;
        }
    }

    pic_jpeg.picture_width  = mPicWidth;
    pic_jpeg.picture_height = mPicHeight;
    pic_jpeg.reconstructed_picture = 0;
    pic_jpeg.coded_buf = va->mCodedBuf;
    status = vaCreateBuffer(va->mDpy, va->mContextId, VAEncPictureParameterBufferType,
                            sizeof(pic_jpeg), 1, &pic_jpeg, &va->mPicParamBuf);
    CHECK_STATUS(status, "vaCreateBuffer", __LINE__)

    status = setJpegQuality(out.quality);
    if (status)
        goto exit;

    status = startJpegEncoding(aSurface);
    if (status)
        goto exit;

    status = vaSyncSurface(va->mDpy, aSurface);
    CHECK_STATUS(status, "vaSyncSurface", __LINE__)

    status = getJpegData(out.buf, out.size, &out.length);

    if(mContextRestoreNeeded) {
        restoreContext();
        mContextRestoreNeeded = false;
    }
exit:
    return (status ? -1 : 0);
}

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
int JpegHwEncoder::encodeAsync(const JpegCompressor::InputBuffer &in, JpegCompressor::OutputBuffer &out)
{
    LOG1("@%s", __FUNCTION__);
    int status = 0;
    VASurfaceID aSurface = 0;
    VAEncPictureParameterBufferJPEG pic_jpeg;
    vaJpegContext *va = mVaEncoderContext;
    mContextRestoreNeeded = false;
    LOG1("input buffer address: %p", in.buf);

    aSurface = va->mBuff2SurfId.valueFor((unsigned int)in.buf);
    if(aSurface == ERROR_POINTER_NOT_FOUND) {
        LOGW("Received buffer does not map to any surface");
        status = resetContext(in, &aSurface);
        mContextRestoreNeeded = true;
        if(status) {
            LOGE("Encoder failed to reset the libVA context");
            return -1;
        }
    }

    pic_jpeg.picture_width  = out.width;
    pic_jpeg.picture_height = out.height;
    pic_jpeg.reconstructed_picture = 0;
    pic_jpeg.coded_buf = va->mCodedBuf;
    status = vaCreateBuffer(va->mDpy, va->mContextId, VAEncPictureParameterBufferType,
                            sizeof(pic_jpeg), 1, &pic_jpeg, &(va->mPicParamBuf));
    CHECK_STATUS(status, "vaCreateBuffer", __LINE__)

    status = setJpegQuality(out.quality);
    if (status)
        goto exit;

    status = startJpegEncoding(aSurface);
    if (status)
        goto exit;

    va->mCurrentSurface = aSurface;

exit:
    return (status ? -1 : 0);
}

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
int JpegHwEncoder::waitToComplete(int *jpegSize)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    vaJpegContext *va = mVaEncoderContext;

    if (va->mCurrentSurface == 0)
        return -1;

    status = vaSyncSurface(va->mDpy, va->mCurrentSurface);
    CHECK_STATUS(status, "vaSyncSurface", __LINE__)

    status = getJpegSize(jpegSize);

    return (status ? -1 : 0);
}

/**
 *  Retrieve the encoded bitstream
 *
 *  Part of the asynchronous encoding process.
 *  Copies the jpeg bistream from the internal buffer allocated by libVA
 *  to the one provided inside the outputBuffer struct
 *
 *  \param out [in] buffer descriptor for the output of the encoding process
 */
int JpegHwEncoder::getOutput(JpegCompressor::OutputBuffer &out)
{
    LOG1("@%s", __FUNCTION__);
    int status = 0;

    status = getJpegData(out.buf, out.size, &out.length);
    if(status) {
        LOGE("Could not retrieved compressed data!");
        return status;
    }

    if(mContextRestoreNeeded) {
        status = restoreContext();
        mContextRestoreNeeded = false;
    }
    return status;
}

/****************************************************************************
 *  PRIVATE METHODS
 ****************************************************************************/

int JpegHwEncoder::configSurfaces(AtomBuffer* inputBuffersArray, int inputBuffersNum)
{
    LOG1("@%s, bufNum:%d, cameraBufArray:%p",
          __FUNCTION__, inputBuffersNum, inputBuffersArray);

    VAStatus status;
    vaJpegContext *va = mVaEncoderContext;
    VASurfaceAttributeTPI           surfaceAttrib;
    memset(&surfaceAttrib, 0, sizeof(surfaceAttrib));

    if(mVaInputSurfacesNum != 0)
        destroySurfaces();

    mPicWidth = inputBuffersArray[0].width;
    mPicHeight = inputBuffersArray[0].height;
    mMaxOutJpegBufSize = inputBuffersArray[0].size;
    mVaInputSurfacesNum = inputBuffersNum;
    if (mPicHeight % 2) {
        LOG1("@%s, line:%d, height:%d, we can't support", __FUNCTION__, __LINE__, mPicHeight);
        return -1;
    }

    CLIP(mVaInputSurfacesNum, MAX_BURST_BUFFERS, 1);

    for (int i = 0 ; i < mVaInputSurfacesNum; i++) {
        mBuffers[i] = (unsigned int) inputBuffersArray[i].buff->data;
    }

    surfaceAttrib.buffers = mBuffers;
    surfaceAttrib.count = mVaInputSurfacesNum;
    surfaceAttrib.luma_stride = mPicWidth;
    surfaceAttrib.pixel_format = VA_FOURCC_NV12;
    surfaceAttrib.width = mPicWidth;
    surfaceAttrib.height = mPicHeight;
    surfaceAttrib.type = VAExternalMemoryUserPointer;
    status = vaCreateSurfacesWithAttribute(va->mDpy, mPicWidth, mPicHeight, va->mSupportedFormat,
                                          mVaInputSurfacesNum, va->mSurfaceIds, &surfaceAttrib);
    CHECK_STATUS(status, "vaCreateSurfacesWithAttribute", __LINE__)


    status = vaCreateContext(va->mDpy, va->mConfigId, mPicWidth, mPicHeight,
                             VA_PROGRESSIVE, va->mSurfaceIds, mVaInputSurfacesNum, &va->mContextId);
    CHECK_STATUS(status, "vaCreateContext", __LINE__)

    /* Create mapping vector from buffer address to surface id */
    for (int i = 0 ; i < mVaInputSurfacesNum; i++) {
        va->mBuff2SurfId.add(mBuffers[i], va->mSurfaceIds[i]);
    }

    /* Allocate buffer for compressed  output. It is stored in mCodedBuf */
    status = vaCreateBuffer(va->mDpy, va->mContextId, VAEncCodedBufferType,
                            mMaxOutJpegBufSize, 1, NULL, &va->mCodedBuf);
    CHECK_STATUS(status, "vaCreateBuffer", __LINE__)

    va->mCurrentSurface = 0;
    return 0;
}

/**
 *  Set the JPEG Q factor
 *
 * This function is used to set the jpeg quality
 *
 * \param quality: one value from 0 to 100
 */
int JpegHwEncoder::setJpegQuality(int quality)
{
    LOG1("@%s, quality:%d", __FUNCTION__, quality);
    VAStatus status;
    vaJpegContext *va = mVaEncoderContext;
    unsigned char *luma_matrix = va->mQMatrix.lum_quantiser_matrix;
    unsigned char *chroma_matrix = va->mQMatrix.chroma_quantiser_matrix;

    // the below are optimal quantization steps for JPEG encoder
    // Those values are provided by JPEG standard
    va->mQMatrix.load_lum_quantiser_matrix = 1;
    va->mQMatrix.load_chroma_quantiser_matrix = 1;

static const unsigned char StandardQuantLuma[64] = {
    16, 11, 10, 16, 24, 40, 51, 61,
    12, 12, 14, 19, 26, 58, 60, 55,
    14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62,
    18, 22, 37, 56, 68, 109, 103, 77,
    24, 35, 55, 64, 81, 104, 113, 92,
    49, 64, 78, 87, 103, 121, 120, 101,
    72, 92, 95, 98, 112, 100, 103, 99
};
static const unsigned char StandardQuantChroma[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99
};

    uint32_t uc_qVal;
    uint32_t uc_j;
    unsigned short ui16_q_factor;
    // Only use 2 tables
    // set the quality to the same as libjpeg, libjpeg support from 1 to 100
    ui16_q_factor = CLIP(quality, 100, 1); // set the quality to [1 to 100]

    // This formula is provided by IJG which is the owner of the libjpeg.
    // JPEG standard doesn't have the concept "quality" at all.
    // Every encoder can design their own quality formula,
    // But most of them follow libjpeg's formula, a widely accepted one.
    ui16_q_factor = (ui16_q_factor < 50) ? (5000 / ui16_q_factor) : (200 - ui16_q_factor * 2);
    for(uc_j = 0; uc_j < 64; uc_j++) {
        uc_qVal = (StandardQuantLuma[uc_j] * ui16_q_factor + 50) / 100;
        luma_matrix[uc_j] = (unsigned char)CLIP(uc_qVal, 255, 1);
    }
    for(uc_j = 0; uc_j < 64; uc_j++) {
        uc_qVal = (StandardQuantChroma[uc_j] * ui16_q_factor + 50) / 100;
        chroma_matrix[uc_j] = (unsigned char)CLIP(uc_qVal, 255, 1);
    }

    status = vaCreateBuffer(va->mDpy, va->mContextId, VAQMatrixBufferType,
                sizeof(va->mQMatrix), 1, &va->mQMatrix, &va->mQMatrixBuf);

    return 0;
}

int JpegHwEncoder::startJpegEncoding(VASurfaceID aSurface)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    vaJpegContext *va = mVaEncoderContext;

    status = vaBeginPicture(va->mDpy, va->mContextId, aSurface);
    CHECK_STATUS(status, "vaBeginPicture", __LINE__)

    status = vaRenderPicture(va->mDpy, va->mContextId, &va->mQMatrixBuf, 1);
    CHECK_STATUS(status, "vaRenderPicture", __LINE__)

    status = vaRenderPicture(va->mDpy, va->mContextId, &va->mPicParamBuf, 1);
    CHECK_STATUS(status, "vaRenderPicture", __LINE__)

    status = vaEndPicture(va->mDpy, va->mContextId);
    CHECK_STATUS(status, "vaEndPicture", __LINE__)

    return 0;
}

int JpegHwEncoder::getJpegSize(int *jpegSize)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    VACodedBufferSegment *buf_list = NULL;
    vaJpegContext *va = mVaEncoderContext;

    if (NULL == jpegSize) {
        LOGE("@%s, line:%d, jpegSize:%p", __FUNCTION__, __LINE__, jpegSize);
        return -1;
    }
    *jpegSize = 0;
    status = vaMapBuffer(va->mDpy, va->mCodedBuf, (void **)&(va->mCodedBufList));
    CHECK_STATUS(status, "vaMapBuffer", __LINE__)

    buf_list = va->mCodedBufList;

    while (buf_list != NULL) {
        *jpegSize += buf_list->size;
        buf_list = (VACodedBufferSegment *)buf_list->next;
    }

    LOG1("@%s, jpeg size:%d", __FUNCTION__, *jpegSize);

    // We will unmap the mCodedBuf when we read the data in getOutput()
    return 0;
}


/**
 * Copies the JPEG bitstream to the user provided buffer
 *
 * It copies the JPEG bitstream from the VA buffer (mCodedBuf)
 * to the user provided buffer dstPtr
 *
 * HW encoder provides the bitstream with the SOI and EOI markers
 * Since we used Exif metadata we need to remove those from the beginning and
 * end of the bitstream
 *
 * It also returns the size of the actual JPEG bitstream
 * Although we are currently reporting the size with the markers
 * otherwise the resulting JPEG's are not valid
 *
 * \param pdst [in] pointer to the user provided
 * \param dstSize [in] size of the user provided buffer
 * \param jpegSize [out] pointer to the int that stores the actual size
 * of the JPEG bitstream
 *
 * \return 0 for success
 * \return -1 for failure
 */
int JpegHwEncoder::getJpegData(void *dstPtr, int dstSize, int *jpegSize)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    VACodedBufferSegment *bufferList = NULL;
    vaJpegContext *va = mVaEncoderContext;
    int segmentSize = 0;
    int writtenSize = 0;
    unsigned char *p = (unsigned char *)dstPtr;
    unsigned char *src;

    if (NULL == dstPtr || NULL == jpegSize) {
        LOGE("@%s, line:%d, dstPtr:%p, jpegSize:%p",
              __FUNCTION__, __LINE__, dstPtr, jpegSize);
        return -1;
    }

    if(va->mCodedBufList == NULL) {
        status = vaMapBuffer(va->mDpy, va->mCodedBuf, (void **)&va->mCodedBufList);
        CHECK_STATUS(status, "vaMapBuffer", __LINE__)
    }

    bufferList = va->mCodedBufList;

    // copy jpeg buffer data from libva to out buffer but skip the JPEG SOI Marker
    src = (unsigned char *)bufferList->buf + SIZE_OF_JPEG_MARKER;
    segmentSize = bufferList->size - SIZE_OF_JPEG_MARKER;

    while (bufferList != NULL) {

        if(bufferList->next == NULL)    // Do not copy EOI marker at the end
            segmentSize -=  SIZE_OF_JPEG_MARKER;

        writtenSize += segmentSize;

        if (writtenSize > dstSize) {
            LOGE("@%s, line:%d, generated JPEG size(%d) is too big > provided buffer(%d)",
                 __FUNCTION__, __LINE__, writtenSize, dstSize);
            return -1;
        }
        memcpy(p, src, segmentSize);

        p +=  segmentSize;
        bufferList = (VACodedBufferSegment *)bufferList->next;
        if(bufferList != NULL) {
            src = (unsigned char *) bufferList->buf;
            segmentSize = bufferList->size;
        }
    }

    // Apparently we need to report the size with the markers even if now we do
    // not copy them anymore.
    *jpegSize = writtenSize + 2*SIZE_OF_JPEG_MARKER;
    LOG1("@%s, line:%d, jpeg size:%d", __FUNCTION__, __LINE__, writtenSize);

    status = vaUnmapBuffer(va->mDpy, va->mCodedBuf);
    CHECK_STATUS(status, "vaUnmapBuffer", __LINE__)
    va->mCodedBufList = NULL;

    return 0;
}

int JpegHwEncoder::destroySurfaces(void)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    vaJpegContext *va = mVaEncoderContext;

    if (va->mDpy && va->mContextId) {
        status = vaDestroyContext(va->mDpy, va->mContextId);
        CHECK_STATUS(status, "vaDestroyContext", __LINE__)
    }
    if (va->mDpy && va->mBuff2SurfId.size() != 0) {
        status = vaDestroySurfaces(va->mDpy, va->mSurfaceIds, va->mBuff2SurfId.size());
        CHECK_STATUS(status, "vaDestroyContext", __LINE__)
    }

    va->mBuff2SurfId.clear();
    va->mContextId = 0;
    return 0;
}

/**
 *  Resets the current libVA context and creates a new one with only 1 surface
 *  This is used when the encoder receives an input frame pointer to encode
 *  that is not mapped to a surface.
 *  A call to restoreContext is needed to revert this operation
 *
 *  \param in Input image buffer descriptor
 *  \param aSurface VASurfaceID of the new surface created in the new context
 *
 *  \return 0 on success
 *  \return -1 on failure
 *  \return Other VAStatus values in case of failure
 */
int JpegHwEncoder::resetContext(const JpegCompressor::InputBuffer &in, unsigned int* aSurface)
{
    LOG1("@%s", __FUNCTION__);

    VAStatus status;
    vaJpegContext *va = mVaEncoderContext;
    VASurfaceAttributeTPI  surfaceAttrib;
    memset(&surfaceAttrib, 0, sizeof(surfaceAttrib));

    deInit();
    init();

    mMaxOutJpegBufSize = in.size;

    if (mPicHeight % 2) {
        LOG1("@%s, line:%d, height:%d, we can't support", __FUNCTION__, __LINE__, mPicHeight);
        return -1;
    }

    surfaceAttrib.buffers = (unsigned int*)&(in.buf);
    surfaceAttrib.count = 1;
    surfaceAttrib.luma_stride = in.width;
    surfaceAttrib.pixel_format = VA_FOURCC_NV12;
    surfaceAttrib.width = in.width;
    surfaceAttrib.height = in.height;
    surfaceAttrib.type = VAExternalMemoryUserPointer;
    status = vaCreateSurfacesWithAttribute(va->mDpy, in.width, in.height, va->mSupportedFormat,
                                          1, (VASurfaceID*)aSurface, &surfaceAttrib);
    CHECK_STATUS(status, "vaCreateSurfacesWithAttribute", __LINE__)


    status = vaCreateContext(va->mDpy, va->mConfigId, in.width, in.height,
                             VA_PROGRESSIVE, aSurface, 1, &va->mContextId);
    CHECK_STATUS(status, "vaCreateContext", __LINE__)

    va->mBuff2SurfId.add((unsigned int)in.buf, *aSurface);

    /* Allocate buffer for compressed  output. It is stored in mCodedBuf */
    status = vaCreateBuffer(va->mDpy, va->mContextId, VAEncCodedBufferType,
                            in.size, 1, NULL, &va->mCodedBuf);
    CHECK_STATUS(status, "vaCreateBuffer", __LINE__)

    va->mCurrentSurface = 0;
    return 0;
}

/**
 *  Restores the libVA context with the buffers originally allocated by PictureThread
 *  that were passed to encoder in setInputBuffers()
 *  This method is only needed if a context was reset. This is track by the boolean
 *  member mContextRestoreNeeded.
 *
 *  \return 0 on success
 *  \return -1 on failure
 */
int JpegHwEncoder::restoreContext()
{
    LOG1("@%s", __FUNCTION__);

    VAStatus status = 0;
    vaJpegContext *va = mVaEncoderContext;
    VASurfaceAttributeTPI  surfaceAttrib;
    memset(&surfaceAttrib, 0, sizeof(surfaceAttrib));

    deInit();
    init();

    surfaceAttrib.buffers = mBuffers;
    surfaceAttrib.count = mVaInputSurfacesNum;
    surfaceAttrib.luma_stride = mPicWidth;
    surfaceAttrib.pixel_format = VA_FOURCC_NV12;
    surfaceAttrib.width = mPicWidth;
    surfaceAttrib.height = mPicHeight;
    surfaceAttrib.type = VAExternalMemoryUserPointer;
    status = vaCreateSurfacesWithAttribute(va->mDpy, mPicWidth, mPicHeight, va->mSupportedFormat,
                                        mVaInputSurfacesNum, va->mSurfaceIds, &surfaceAttrib);
    CHECK_STATUS(status, "vaCreateSurfacesWithAttribute", __LINE__)


    status = vaCreateContext(va->mDpy, va->mConfigId, mPicWidth, mPicHeight,
                           VA_PROGRESSIVE, va->mSurfaceIds, mVaInputSurfacesNum, &va->mContextId);
    CHECK_STATUS(status, "vaCreateContext", __LINE__)

    /* clear possible leftover from previous reset */
    va->mBuff2SurfId.clear();

    /* Create mapping vector from buffer address to surface id */
    for (int i = 0 ; i < mVaInputSurfacesNum; i++) {
      va->mBuff2SurfId.add(mBuffers[i], va->mSurfaceIds[i]);
    }

    /* Allocate buffer for compressed  output. It is stored in mCodedBuf */
    mMaxOutJpegBufSize = mPicWidth * mPicHeight * 2;
    status = vaCreateBuffer(va->mDpy, va->mContextId, VAEncCodedBufferType,
                          mMaxOutJpegBufSize, 1, NULL, &va->mCodedBuf);
    CHECK_STATUS(status, "vaCreateBuffer", __LINE__)

    va->mCurrentSurface = 0;

    return status;
}
}
