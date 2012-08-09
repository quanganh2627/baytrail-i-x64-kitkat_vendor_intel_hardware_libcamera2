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
#define LOG_TAG "Camera_JpegCompressor"

#define JPEG_BLOCK_SIZE 4096

#include "JpegCompressor.h"
#include "ColorConverter.h"
#include "LogHelper.h"
#include "SkBitmap.h"
#include "SkStream.h"
#include <string.h>

namespace android {

#ifdef USE_INTEL_JPEG
JpegCompressor::WrapperLibVA::WrapperLibVA() :
    mVaDpy(0),
    mConfigId(0),
    mSurfaceId(0),
    mContextId(0),
    mCodedBuf(0),
    mPicParamBuf(0),
    mQMatrixBuf(0),
    mBuffers(0),
    mPicWidth(0),
    mPicHeight(0),
    mMaxOutJpegBufSize(0)
{
    LOG1("@%s", __FUNCTION__);
    memset(&mSurfaceImage, 0, sizeof(mSurfaceImage));
    memset(&mQMatrix, 0, sizeof(mQMatrix));
    memset(&mSurfaceAttrib, 0, sizeof(mSurfaceAttrib));
}

JpegCompressor::WrapperLibVA::~WrapperLibVA()
{
    LOG1("@%s", __FUNCTION__);
}

int JpegCompressor::WrapperLibVA::init(void)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    int display_num, major_ver, minor_ver;
    const char *driver = NULL;
    VAEntrypoint entrypoints[VAEntrypointMax];
    int num_entrypoints, i;
    VAConfigAttrib attrib;
    int maxNum;

    mVaDpy = vaGetDisplay(&display_num);
    status = vaInitialize(mVaDpy, &major_ver, &minor_ver);
    CHECK_STATUS(status, "vaInitialize", __LINE__)

    driver = vaQueryVendorString(mVaDpy);
    maxNum = vaMaxNumEntrypoints(mVaDpy);
    status = vaQueryConfigEntrypoints(mVaDpy, VAProfileJPEGBaseline, entrypoints, &num_entrypoints);
    CHECK_STATUS(status, "vaQueryConfigEntrypoints", __LINE__)
    for(i = 0; i < num_entrypoints; i++) {
        if (entrypoints[i] == VAEntrypointEncPicture)
            break;
    }
    if (i == num_entrypoints) {
        LOGE("@%s, line:%d, not find Slice entry point, num:%d", __FUNCTION__, __LINE__, num_entrypoints);
        return -1;
    }

    attrib.type = VAConfigAttribRTFormat;
    attrib.value = mSupportedFormat;
    status = vaCreateConfig(mVaDpy, VAProfileJPEGBaseline, VAEntrypointEncPicture,
                              &attrib, 1, &mConfigId);
    CHECK_STATUS(status, "vaCreateConfig", __LINE__)

    return 0;
}

int JpegCompressor::WrapperLibVA::doJpegEncoding(void)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;

    status = vaBeginPicture(mVaDpy, mContextId, mSurfaceId);
    CHECK_STATUS(status, "vaBeginPicture", __LINE__)

    status = vaRenderPicture(mVaDpy, mContextId, &mQMatrixBuf, 1);
    CHECK_STATUS(status, "vaRenderPicture", __LINE__)

    status = vaRenderPicture(mVaDpy, mContextId, &mPicParamBuf, 1);
    CHECK_STATUS(status, "vaRenderPicture", __LINE__)

    status = vaEndPicture(mVaDpy, mContextId);
    CHECK_STATUS(status, "vaEndPicture", __LINE__)

    status = vaSyncSurface(mVaDpy, mSurfaceId);
    CHECK_STATUS(status, "vaSyncSurface", __LINE__)

    return 0;
}

int JpegCompressor::WrapperLibVA::getJpegData(void *pdst, int dstSize, int *jpegSize)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    VACodedBufferSegment *buf_list = NULL;
    int write_size = 0;
    unsigned char *p = (unsigned char *)pdst;

    if (NULL == pdst || NULL == jpegSize) {
        LOGE("@%s, line:%d, pdst:%p, jpegSize:%p", __FUNCTION__, __LINE__, pdst, jpegSize);
        return -1;
    }

    status = vaMapBuffer(mVaDpy, mCodedBuf, (void **)&buf_list);
    CHECK_STATUS(status, "vaMapBuffer", __LINE__)

    // copy jpeg buffer data from libva to out buffer
    while (buf_list != NULL) {
        write_size += buf_list->size;
        if (write_size > dstSize) {
            LOGE("@%s, line:%d, generated JPEG size(%d) is too big > provided buffer(%d)", __FUNCTION__, __LINE__, write_size, dstSize);
            return -1;
        }
        memcpy(p, buf_list->buf, buf_list->size);
        p += buf_list->size;
        buf_list = (VACodedBufferSegment *)buf_list->next;
    }

    *jpegSize = write_size;
    LOG1("@%s, line:%d, jpeg size:%d", __FUNCTION__, __LINE__, write_size);

    status = vaUnmapBuffer(mVaDpy, mCodedBuf);
    CHECK_STATUS(status, "vaUnmapBuffer", __LINE__)

    return 0;
}

int JpegCompressor::WrapperLibVA::configSurface(int width, int height, int bufNum, bool useCameraBuf, void *cameraBuf)
{
    LOG1("@%s, bufNum:%d, useCameraBuf:%d, cameraBuf:%p", __FUNCTION__, bufNum, useCameraBuf, cameraBuf);
    VAStatus status;
    void *surface_p = NULL;
    VAEncPictureParameterBufferJPEG pic_jpeg;

    if (height % 2) {
        LOG1("@%s, line:%d, height:%d, we can't support", __FUNCTION__, __LINE__, height);
        return -1;
    }

    if (useCameraBuf && (NULL == cameraBuf)) {
        LOG1("@%s, line:%d, cameraBuf is NULL", __FUNCTION__, __LINE__);
        return -1;
    }

    mPicWidth = width;
    mPicHeight = height;
    mMaxOutJpegBufSize = (width * height * 3 / 2);

    if (useCameraBuf) {
        mBuffers = (unsigned int)cameraBuf;

        mSurfaceAttrib.buffers = &mBuffers;
        mSurfaceAttrib.count = bufNum;
        mSurfaceAttrib.luma_stride = mPicWidth;
        mSurfaceAttrib.pixel_format = VA_FOURCC_NV12;
        mSurfaceAttrib.width = mPicWidth;
        mSurfaceAttrib.height = mPicHeight;
        mSurfaceAttrib.type = VAExternalMemoryUserPointer;
        status = vaCreateSurfacesWithAttribute(mVaDpy, mPicWidth, mPicHeight,
                        mSupportedFormat, bufNum, &mSurfaceId, &mSurfaceAttrib);
        CHECK_STATUS(status, "vaCreateSurfacesWithAttribute", __LINE__)
    } else {
        status = vaCreateSurfaces(mVaDpy, mSupportedFormat, mPicWidth, mPicHeight, &mSurfaceId, bufNum, NULL, 0);
        CHECK_STATUS(status, "vaCreateSurfaces", __LINE__)
    }

    status = vaCreateContext(mVaDpy, mConfigId, mPicWidth, mPicHeight,
                                VA_PROGRESSIVE, &mSurfaceId, bufNum, &mContextId);
    CHECK_STATUS(status, "vaCreateContext", __LINE__)

    status = vaCreateBuffer(mVaDpy, mContextId, VAEncCodedBufferType, mMaxOutJpegBufSize, 1, NULL, &mCodedBuf);
    CHECK_STATUS(status, "vaCreateBuffer", __LINE__)

    return 0;
}

void JpegCompressor::WrapperLibVA::destroySurface(void)
{
    LOG1("@%s", __FUNCTION__);
    if (mVaDpy && mContextId)
        vaDestroyContext(mVaDpy, mContextId);
    if (mVaDpy && mSurfaceId)
        vaDestroySurfaces(mVaDpy, &mSurfaceId, 1);
}

int JpegCompressor::WrapperLibVA::getJpegSrcData(void *pRaw, bool useCameraBuf)
{
    LOG1("@%s, useCameraBuf:%d", __FUNCTION__, useCameraBuf);
    VAStatus status;
    void *surface_p = NULL;
    VAEncPictureParameterBufferJPEG pic_jpeg;

    if (!useCameraBuf) {
        if (NULL == pRaw) {
            LOG1("@%s, line:%d, pRaw is NULL", __FUNCTION__, __LINE__);
            return -1;
        }
        if (mapJpegSrcBuffers(&surface_p) < 0)
            return -1;
        if(copySrcDataToLibVA(pRaw, surface_p) < 0)
            return -1;
        if (unmapJpegSrcBuffers() < 0)
            return -1;
    }

    pic_jpeg.picture_width  = mPicWidth;
    pic_jpeg.picture_height = mPicHeight;
    pic_jpeg.reconstructed_picture = 0;
    pic_jpeg.coded_buf = mCodedBuf;
    status = vaCreateBuffer(mVaDpy, mContextId, VAEncPictureParameterBufferType,
            sizeof(pic_jpeg), 1, &pic_jpeg, &mPicParamBuf);
    CHECK_STATUS(status, "vaCreateBuffer", __LINE__)

    return 0;
}

int JpegCompressor::WrapperLibVA::setJpegQuality(int quality)
{
    LOG1("@%s, quality:%d", __FUNCTION__, quality);
    VAStatus status;
    unsigned char *luma_matrix = mQMatrix.lum_quantiser_matrix;
    unsigned char *chroma_matrix = mQMatrix.chroma_quantiser_matrix;
    /*
        the below are optimal quantization steps for JPEG encoder
        Those values are provided by JPEG standard
    */
    mQMatrix.load_lum_quantiser_matrix = 1;
    mQMatrix.load_chroma_quantiser_matrix = 1;

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
    /*
        This formula is provided by IJG which is the owner of the libjpeg.
        JPEG standard doesn't have the concept "quality" at all.
        Every encoder can design their own quality formula,
        But most of them follow libjpeg's formula, a widely accepted one.
    */
    ui16_q_factor = (ui16_q_factor < 50) ? (5000 / ui16_q_factor) : (200 - ui16_q_factor * 2);
    for(uc_j = 0; uc_j < 64; uc_j++) {
        uc_qVal = (StandardQuantLuma[uc_j] * ui16_q_factor + 50) / 100;
        luma_matrix[uc_j] = (unsigned char)CLIP(uc_qVal, 255, 1);
    }
    for(uc_j = 0; uc_j < 64; uc_j++) {
        uc_qVal = (StandardQuantChroma[uc_j] * ui16_q_factor + 50) / 100;
        chroma_matrix[uc_j] = (unsigned char)CLIP(uc_qVal, 255, 1);
    }

    status = vaCreateBuffer(mVaDpy, mContextId, VAQMatrixBufferType,
                sizeof(mQMatrix), 1, &mQMatrix, &mQMatrixBuf);

    return 0;
}

int JpegCompressor::WrapperLibVA::copySrcDataToLibVA(void *psrc, void *pdst)
{
    LOG1("@%s", __FUNCTION__);
    int i;
    unsigned char *ydata, *uvdata; // src
    unsigned char *row_start, *uv_start; // dst

    if (NULL == psrc || NULL == pdst) {
        LOGE("@%s, line:%d, psrc:%p, pdst:%p", __FUNCTION__, __LINE__, psrc, pdst);
        return -1;
    }

    /* copy Y plane */
    ydata = (unsigned char *)psrc;
    for (i = 0; i < mPicHeight; i++) {
        row_start = (unsigned char *)pdst + i * mSurfaceImage.pitches[0];
        memcpy(row_start, ydata, mPicWidth);
        ydata += mPicWidth;
    }

    /* copy UV data */ /* src is NV12 */
    uvdata = (unsigned char *)psrc + mPicWidth * mPicHeight;
    uv_start = (unsigned char *)pdst + mSurfaceImage.offsets[1];
    for(i = 0; i < (mPicHeight / 2); i++) {
        row_start = (unsigned char *)uv_start + i * mSurfaceImage.pitches[1];
        memcpy(row_start, uvdata, mPicWidth);
        uvdata += mPicWidth;
    }

    LOG1("@%s, line:%d, pitches[0]:%d, pitches[1]:%d, offsets[1]:%d",
        __FUNCTION__, __LINE__, mSurfaceImage.pitches[0],
        mSurfaceImage.pitches[1], mSurfaceImage.offsets[1]);

    return 0;
}

int JpegCompressor::WrapperLibVA::mapJpegSrcBuffers(void **pbuf)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    status = vaDeriveImage(mVaDpy, mSurfaceId, &mSurfaceImage);
    CHECK_STATUS(status, "vaDeriveImage", __LINE__)
    status = vaMapBuffer(mVaDpy, mSurfaceImage.buf, pbuf);
    CHECK_STATUS(status, "vaMapBuffer", __LINE__)

    return 0;
}

int JpegCompressor::WrapperLibVA::unmapJpegSrcBuffers(void)
{
    LOG1("@%s", __FUNCTION__);
    VAStatus status;
    status = vaUnmapBuffer(mVaDpy, mSurfaceImage.buf);
    CHECK_STATUS(status, "vaUnmapBuffer", __LINE__)

    status = vaDestroyImage(mVaDpy, mSurfaceImage.image_id);
    CHECK_STATUS(status, "vaDestroyImage", __LINE__)

    return 0;
}

void JpegCompressor::WrapperLibVA::deInit()
{
    LOG1("@%s", __FUNCTION__);
    if (mVaDpy && mConfigId)
        vaDestroyConfig(mVaDpy, mConfigId);
    if (mVaDpy)
        vaTerminate(mVaDpy);
}
#endif // USE_INTEL_JPEG

JpegCompressor::JpegCompressor() :
    mVaInputSurfacesNum(0)
    ,mVaSurfaceWidth(0)
    ,mVaSurfaceHeight(0)
    ,mJpegCompressStruct(NULL)
    ,mStartSharedBuffersEncode(false)
    ,mStartCompressDone(false)
{
    LOG1("@%s", __FUNCTION__);
    mJpegEncoder = SkImageEncoder::Create(SkImageEncoder::kJPEG_Type);
    if (mJpegEncoder == NULL) {
        LOGE("No memory for Skia JPEG encoder!");
    }
    memset(mVaInputSurfacesPtr, 0, sizeof(mVaInputSurfacesPtr));
}

JpegCompressor::~JpegCompressor()
{
    LOG1("@%s", __FUNCTION__);
    if (mJpegEncoder != NULL) {
        LOG1("Deleting Skia JPEG encoder...");
        delete mJpegEncoder;
    }
}

bool JpegCompressor::convertRawImage(void* src, void* dst, int width, int height, int format)
{
    LOG1("@%s", __FUNCTION__);
    bool ret = true;
    switch (format) {
    case V4L2_PIX_FMT_NV12:
        LOG1("Converting frame from NV12 to RGB565");
        NV12ToRGB565(width, height, src, dst);
        break;
    case V4L2_PIX_FMT_YUV420:
        LOG1("Converting frame from YUV420 to RGB565");
        YUV420ToRGB565(width, height, src, dst);
        break;
    default:
        LOGE("Unsupported color format: %s", v4l2Fmt2Str(format));
        ret = false;
    }
    return ret;
}

int JpegCompressor::swEncode(const InputBuffer &in, const OutputBuffer &out)
{
    LOG1("@%s", __FUNCTION__);

    // For SW path
    SkBitmap skBitmap;
    SkDynamicMemoryWStream skStream;

    // Choose Skia
    LOG1("Choosing Skia for JPEG encoding");
    if (mJpegEncoder == NULL) {
        LOGE("Skia JpegEncoder not created, cannot encode to JPEG!");
        return -1;
    }
    bool success = convertRawImage((void*)in.buf, (void*)out.buf, in.width, in.height, in.format);
    if (!success) {
        LOGE("Could not convert the raw image!");
        return -1;
    }
    skBitmap.setConfig(SkBitmap::kRGB_565_Config, in.width, in.height);
    skBitmap.setPixels(out.buf, NULL);
    LOG1("Encoding stream using Skia...");
    if (mJpegEncoder->encodeStream(&skStream, skBitmap, out.quality)) {
        mJpegSize = skStream.getOffset();
        skStream.copyTo(out.buf);
    } else {
        LOGE("Skia could not encode the stream!");
        return -1;
    }

    return 0;
}

#ifdef USE_INTEL_JPEG
int JpegCompressor::hwEncode(const InputBuffer &in, const OutputBuffer &out)
{
    LOG1("@%s", __FUNCTION__);
    int status;
    const bool use_camera_buf = true;  // true:use camera buf,false:use video buf

    status = mLibVA.init();
    if (status)
        goto exit;

    status = mLibVA.configSurface(in.width, in.height, 1, use_camera_buf, in.buf);
    if (status)
        goto exit;

    status = mLibVA.getJpegSrcData(in.buf, use_camera_buf);
    if (status)
        goto exit;

    status = mLibVA.setJpegQuality(out.quality);
    if (status)
        goto exit;

    status = mLibVA.doJpegEncoding();
    if (status)
        goto exit;

    status = mLibVA.getJpegData(out.buf, out.size, &mJpegSize);
    if (status)
        goto exit;

exit:
    mLibVA.destroySurface();
    mLibVA.deInit();

    return (status ? -1 : 0);
}
#endif // USE_INTEL_JPEG

// Takes YUV data (NV12 or YUV420) and outputs JPEG encoded stream
int JpegCompressor::encode(const InputBuffer &in, const OutputBuffer &out)
{
    LOG1("@%s:\n\t IN  = {buf:%p, w:%u, h:%u, sz:%u, f:%s}" \
             "\n\t OUT = {buf:%p, w:%u, h:%u, sz:%u, q:%d}",
            __FUNCTION__,
            in.buf, in.width, in.height, in.size, v4l2Fmt2Str(in.format),
            out.buf, out.width, out.height, out.size, out.quality);

    if (in.width == 0 || in.height == 0 || in.format == 0) {
        LOGE("Invalid input received!");
        mJpegSize = -1;
        goto exit;
    }
    // Decide the encoding path: Skia or libva
    /*
     * jpeglib can encode images using libva only if the image format is NV12 and
     * image sizes are greater than 320x240. If the image does not meet this criteria,
     * then encode it using Skia. For Skia, it is required an additional step:
     * the color conversion of the image to RGB565.
     * The 320x240 size was found in external/jpeg/jcapistd.c:27,28
     */
    if ((in.width <= 320 && in.height <= 240) || in.format != V4L2_PIX_FMT_NV12) {
        if (swEncode(in, out) < 0)
            goto exit;
    } else {
#ifdef USE_INTEL_JPEG
        LOG1("Choosing libva for HW JPEG encoding");
        if (hwEncode(in, out) < 0) {
            if (swEncode(in, out) < 0)
                goto exit;
        }
#else
        if (swEncode(in, out) < 0)
            goto exit;
#endif
    }

    return mJpegSize;
exit:
    return (mJpegSize = -1);
}

// Starts encoding of multiple shared buffers
status_t JpegCompressor::startSharedBuffersEncode(void *outBuf, int outSize)
{
    LOG1("@%s", __FUNCTION__);

    return NO_ERROR;
}

// Stops encoding of multiple shared buffers
status_t JpegCompressor::stopSharedBuffersEncode()
{
    LOG1("@%s", __FUNCTION__);

    return NO_ERROR;
}

status_t JpegCompressor::getSharedBuffers(int width, int height, void** sharedBuffersPtr, int sharedBuffersNum)
{
    LOG1("@%s", __FUNCTION__);

    return NO_ERROR;
}

}
