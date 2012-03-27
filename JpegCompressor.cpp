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
#define LOG_TAG "Atom_JpegCompressor"

#define JPEG_BLOCK_SIZE 4096

#include "JpegCompressor.h"
#include "ColorConverter.h"
#include "LogHelper.h"
#include "SkBitmap.h"
#include "SkStream.h"
#include <string.h>

#ifdef USE_INTEL_JPEG
#include <va/va.h>
#endif
extern "C" {
    #include "jpeglib.h"
    #include "jpeglib_ext.h"
}

namespace android {
/*
 * START: jpeglib interface functions
 */

// jpeg destination manager structure
struct JpegDestinationManager {
    struct jpeg_destination_mgr pub; // public fields
    JSAMPLE *encodeBlock;            // encode block buffer
    JSAMPLE *outJpegBuf;             // JPEG output buffer
    JSAMPLE *outJpegBufPos;          // JPEG output buffer current ptr
    int outJpegBufSize;              // JPEG output buffer size
    int *dataCount;                  // JPEG output buffer data written count
};

// initialize the jpeg compression destination buffer (passed to libjpeg as function pointer)
static void init_destination(j_compress_ptr cinfo)
{
    LOG1("@%s", __FUNCTION__);
    JpegDestinationManager *dest = (JpegDestinationManager*) cinfo->dest;
    dest->encodeBlock = (JSAMPLE *)(*cinfo->mem->alloc_small) \
            ((j_common_ptr) cinfo, JPOOL_IMAGE, JPEG_BLOCK_SIZE * sizeof(JSAMPLE));
    dest->pub.next_output_byte = dest->encodeBlock;
    dest->pub.free_in_buffer = JPEG_BLOCK_SIZE;
}

// handle the jpeg output buffers (passed to libjpeg as function pointer)
static boolean empty_output_buffer(j_compress_ptr cinfo)
{
    LOG2("@%s", __FUNCTION__);
    JpegDestinationManager *dest = (JpegDestinationManager*) cinfo->dest;
    if(dest->outJpegBufSize < *(dest->dataCount) + JPEG_BLOCK_SIZE)
    {
        LOGE("JPEGLIB: empty_output_buffer overflow!");
        *(dest->dataCount) = 0;
        return FALSE;
    }
    memcpy(dest->outJpegBufPos, dest->encodeBlock, JPEG_BLOCK_SIZE);
    dest->outJpegBufPos += JPEG_BLOCK_SIZE;
    *(dest->dataCount) += JPEG_BLOCK_SIZE;
    dest->pub.next_output_byte = dest->encodeBlock;
    dest->pub.free_in_buffer = JPEG_BLOCK_SIZE;
    return TRUE;
}

// terminate the compression destination buffer (passed to libjpeg as function pointer)
static void term_destination(j_compress_ptr cinfo)
{
    LOG1("@%s", __FUNCTION__);
    JpegDestinationManager *dest = (JpegDestinationManager*) cinfo->dest;
    unsigned dataCount = JPEG_BLOCK_SIZE - dest->pub.free_in_buffer;
    if(dest->outJpegBufSize < dataCount)
    {
        *(dest->dataCount) = 0;
        return;
    }
    memcpy(dest->outJpegBufPos, dest->encodeBlock, dataCount);
    dest->outJpegBufPos += dataCount;
    *(dest->dataCount) += dataCount;
}

// setup the destination manager in j_compress_ptr handle
static int setup_jpeg_destmgr(j_compress_ptr cinfo, JSAMPLE *outBuf, int jpegBufSize, int *jpegSizePtr)
{
    LOG1("@%s", __FUNCTION__);
    JpegDestinationManager *dest;

    if(outBuf == NULL || jpegBufSize <= 0 )
        return -1;

    LOG1("Setting up JPEG destination manager...");
    dest = (JpegDestinationManager*) cinfo->dest;
    if (cinfo->dest == NULL) {
        LOG1("Create destination manager...");
        cinfo->dest = (struct jpeg_destination_mgr *)(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(JpegDestinationManager));
        dest = (JpegDestinationManager*) cinfo->dest;
        dest->pub.init_destination = init_destination;
        dest->pub.empty_output_buffer = empty_output_buffer;
        dest->pub.term_destination = term_destination;
        dest->outJpegBuf = outBuf;
    }
    LOG1("Out: bufPos = %p, bufSize = %d, dataCount = %d", outBuf, jpegBufSize, *jpegSizePtr);
    dest->outJpegBufSize = jpegBufSize;
    dest->outJpegBufPos = outBuf;
    dest->dataCount = jpegSizePtr;
    return 0;
}

/*
 * END: jpeglib interface functions
 */

JpegCompressor::JpegCompressor() :
    mVaInputSurfacesNum(0)
    ,mVaSurfaceWidth(0)
    ,mVaSurfaceHeight(0)
    ,mJpegCompressStruct(NULL)
    ,mStartSharedBuffersEncode(false)
#ifndef ANDROID_1998
    ,mStartCompressDone(false)
#endif
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

// Takes YUV data (NV12 or YUV420) and outputs JPEG encoded stream
int JpegCompressor::encode(const InputBuffer &in, const OutputBuffer &out)
{
    LOG1("@%s:\n\t IN  = {buf:%p, w:%u, h:%u, sz:%u, f:%s}" \
             "\n\t OUT = {buf:%p, w:%u, h:%u, sz:%u, q:%d}",
            __FUNCTION__,
            in.buf, in.width, in.height, in.size, v4l2Fmt2Str(in.format),
            out.buf, out.width, out.height, out.size, out.quality);
    // For SW path
    SkBitmap skBitmap;
    SkDynamicMemoryWStream skStream;
    // For HW path
    struct jpeg_compress_struct cinfo;
    struct jpeg_compress_struct* pCinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];

    if (in.width == 0 || in.height == 0 || in.format == 0) {
        LOGE("Invalid input received!");
        mJpegSize = -1;
        goto exit;
    }
    // Decide the encoding path: Skia or libjpeg
    /*
     * jpeglib can encode images using libva only if the image format is NV12 and
     * image sizes are greater than 320x240. If the image does not meet this criteria,
     * then encode it using Skia. For Skia, it is required an additional step:
     * the color conversion of the image to RGB565.
     * The 320x240 size was found in external/jpeg/jcapistd.c:27,28
     */
#ifdef USE_INTEL_JPEG
    if ((in.width <= 320 && in.height <= 240) || in.format != V4L2_PIX_FMT_NV12)
#endif
    {
        // Choose Skia
        LOG1("Choosing Skia for JPEG encoding");
        if (mJpegEncoder == NULL) {
            LOGE("Skia JpegEncoder not created, cannot encode to JPEG!");
            mJpegSize = -1;
            goto exit;
        }
        bool success = convertRawImage((void*)in.buf, (void*)out.buf, in.width, in.height, in.format);
        if (!success) {
            LOGE("Could not convert the raw image!");
            mJpegSize = -1;
            goto exit;
        }
        skBitmap.setConfig(SkBitmap::kRGB_565_Config, in.width, in.height);
        skBitmap.setPixels(out.buf, NULL);
        LOG1("Encoding stream using Skia...");
        if (mJpegEncoder->encodeStream(&skStream, skBitmap, out.quality)) {
            mJpegSize = skStream.getOffset();
            skStream.copyTo(out.buf);
        } else {
            LOGE("Skia could not encode the stream!");
            mJpegSize = -1;
            goto exit;
        }
    }
#ifdef USE_INTEL_JPEG
    else
    {
        // Choose jpeglib
        LOG1("Choosing jpeglib for JPEG encoding");

        char *vaSurface = NULL;
        bool sharedBufferCompress = false;
        // Verify the validity of input buffer (it MUST be a VA surface)
        for (int i = 0; i < mVaInputSurfacesNum && mJpegCompressStruct != NULL; i++) {
            if ((char*)in.buf == mVaInputSurfacesPtr[i]) {
                vaSurface = mVaInputSurfacesPtr[i];
                LOG1("Using shared buffer %u @%p as input buffer", i, vaSurface);
                sharedBufferCompress = true;
                break;
            }
        }

        if (sharedBufferCompress) {
            // If this is a shared buffer, no need for create compress, this is already done
            pCinfo = (struct jpeg_compress_struct*)mJpegCompressStruct;
        } else {
            memset(&cinfo, 0, sizeof(cinfo));
            memset(&jerr, 0, sizeof(jerr));
            pCinfo = &cinfo;
            pCinfo->err = jpeg_std_error (&jerr);
            jpeg_create_compress(pCinfo);
        }

        mJpegSize = 0;
        setup_jpeg_destmgr(pCinfo, static_cast<JSAMPLE*>(out.buf), out.size, &mJpegSize);

        // If the input pointer is not a VA surface, then copy it to a valid VA surface
        if (vaSurface == NULL) {
            LOG1("Get a VA surface from JPEG encoder...");
            if(!jpeg_get_userptr_from_surface(pCinfo, in.width, in.height, VA_FOURCC_NV12, &vaSurface)) {
                LOGE("Failed to get user pointer");
                jpeg_destroy_compress(pCinfo);
                mJpegSize = -1;
                goto exit;
            }

            LOG1("Copy NV12 image to VA surface @%p: %d bytes", vaSurface, in.size);
            memcpy(vaSurface, in.buf, in.size);
        }

        pCinfo->image_width = in.width;
        pCinfo->image_height = in.height;
        pCinfo->input_components = 3;
        pCinfo->in_color_space = JCS_YCbCr;
        jpeg_set_defaults(pCinfo);
        jpeg_set_colorspace(pCinfo, JCS_YCbCr);
        jpeg_set_quality(pCinfo, out.quality, TRUE);
        pCinfo->raw_data_in = TRUE;
        pCinfo->dct_method = JDCT_FLOAT;
        pCinfo->comp_info[0].h_samp_factor = pCinfo->comp_info[0].v_samp_factor = 2;
        pCinfo->comp_info[1].h_samp_factor = pCinfo->comp_info[1].v_samp_factor = 1;
        pCinfo->comp_info[2].h_samp_factor = pCinfo->comp_info[2].v_samp_factor = 1;

        LOG1("Start compression...");
        jpeg_start_compress(pCinfo, TRUE);
#ifndef ANDROID_1998
        mStartCompressDone = true;
#endif

        LOG1("Compressing...");
        jpeg_write_raw_data(pCinfo, (JSAMPIMAGE)vaSurface, pCinfo->image_height);

        LOG1("Finish compression...");
        jpeg_finish_compress(pCinfo);

        if (!sharedBufferCompress) {
            jpeg_destroy_compress(pCinfo);
        }
    }
#endif

exit:
    return mJpegSize;
}

// Starts encoding of multiple shared buffers
status_t JpegCompressor::startSharedBuffersEncode(void *outBuf, int outSize)
{
    LOG1("@%s", __FUNCTION__);
#ifdef USE_INTEL_JPEG
    static struct jpeg_compress_struct cinfo;
    static struct jpeg_error_mgr jerr;
    if (mStartSharedBuffersEncode && mJpegCompressStruct != NULL) {
        JpegDestinationManager* dest = (JpegDestinationManager*) cinfo.dest;
        LOG1("Our output buffer: %p (sz: %d), received output buffer: %p (sz: %d)",
                dest->outJpegBuf, dest->outJpegBufSize,
                outBuf, outSize);
        if (dest->outJpegBuf == outBuf && dest->outJpegBufSize == outSize) {
            // Nothing to do, we already started with this configuration
            return NO_ERROR;
        } else {
            // Free previous allocated buffers
            stopSharedBuffersEncode();
        }
    }
    memset(&cinfo , 0, sizeof(cinfo));
    memset(&jerr , 0, sizeof(jerr));
    cinfo.err = jpeg_std_error (&jerr);
    LOG1("Starting new shared buffers compress on: %p", &cinfo);
    jpeg_create_compress(&cinfo);
    mJpegSize = 0;
    setup_jpeg_destmgr(&cinfo, static_cast<JSAMPLE*>(outBuf), outSize, &mJpegSize);
    mJpegCompressStruct = &cinfo;
    mStartSharedBuffersEncode = true;
#ifndef ANDROID_1998
    mStartCompressDone = false;
#endif
#endif
    return NO_ERROR;
}

// Stops encoding of multiple shared buffers
status_t JpegCompressor::stopSharedBuffersEncode()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    struct jpeg_compress_struct *pCinfo = NULL;
    if (!mStartSharedBuffersEncode) {
        // already stopped, nothing to do
        return NO_ERROR;
    }
#ifdef USE_INTEL_JPEG
    pCinfo = (struct jpeg_compress_struct*)mJpegCompressStruct;
    LOG1("Stopping shared buffers compress on: %p", pCinfo);
#ifndef ANDROID_1998
    if (!mStartCompressDone) {
        /*
         * Before calling destroy_compress, make fake calls to start_compress
         * so the libjpeg will initialize the hw_path and free the libva memory
         * in destroy_compress. Without calling start_compress, hw_path from
         * libjpeg is not initialized, therefore libva resources WON'T be freed
         * in destroy_compress.
         */
        LOG1("Fake Start compression...");
        jpeg_start_compress(pCinfo, TRUE);
    }
#endif
    jpeg_destroy_compress(pCinfo);
    mJpegCompressStruct = NULL;
    mStartSharedBuffersEncode = false;
    mVaInputSurfacesNum = 0;
    mVaSurfaceWidth = 0;
    mVaSurfaceHeight = 0;
#endif
    return NO_ERROR;
}

status_t JpegCompressor::getSharedBuffers(int width, int height, void** sharedBuffersPtr, int sharedBuffersNum)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    int vaSurfacesNum = 0;
    struct jpeg_compress_struct *pCinfo = NULL;
    if (!mStartSharedBuffersEncode) {
        LOGE("Shared buffer encoding session is not started!");
        return INVALID_OPERATION;
    }
    LOG1("Our shared buffers: %dx%d (num: %d), requested buffers: %dx%d (num: %d)",
            mVaSurfaceWidth, mVaSurfaceHeight, mVaInputSurfacesNum,
            width, height, sharedBuffersNum);
    if (mJpegCompressStruct != NULL &&
            mVaInputSurfacesNum == sharedBuffersNum &&
            mVaSurfaceWidth == width &&
            mVaSurfaceHeight == height) {
        // Nothing to do, we already have these shared buffers
        if (sharedBuffersPtr != NULL) {
            *sharedBuffersPtr = mVaInputSurfacesPtr;
        }
        return NO_ERROR;
    }
    pCinfo = (struct jpeg_compress_struct*)mJpegCompressStruct;
#ifdef USE_INTEL_JPEG
    for (vaSurfacesNum = 0; vaSurfacesNum < sharedBuffersNum; vaSurfacesNum++) {
        LOG1("Get a VA surface from JPEG encoder...");
        if(!jpeg_get_userptr_from_surface(pCinfo, width, height, VA_FOURCC_NV12, &mVaInputSurfacesPtr[vaSurfacesNum])) {
            LOGE("Failed to get user pointer");
            status = NO_MEMORY;
            break;
        }
        LOG1("Got VA surface @%p as shared buffer %d", mVaInputSurfacesPtr[vaSurfacesNum], vaSurfacesNum);
    }
    if (status == NO_ERROR) {
        // Initialize cinfo
        pCinfo->image_width = width;
        pCinfo->image_height = height;
        pCinfo->input_components = 3;
        pCinfo->in_color_space = JCS_YCbCr;
        jpeg_set_defaults(pCinfo);
        jpeg_set_colorspace(pCinfo, JCS_YCbCr);
        pCinfo->raw_data_in = TRUE;
        pCinfo->dct_method = JDCT_FLOAT;
        pCinfo->comp_info[0].h_samp_factor = pCinfo->comp_info[0].v_samp_factor = 2;
        pCinfo->comp_info[1].h_samp_factor = pCinfo->comp_info[1].v_samp_factor = 1;
        pCinfo->comp_info[2].h_samp_factor = pCinfo->comp_info[2].v_samp_factor = 1;
        mVaInputSurfacesNum = sharedBuffersNum;
        mVaSurfaceWidth = width;
        mVaSurfaceHeight = height;
        if (sharedBuffersPtr != NULL) {
            *sharedBuffersPtr = mVaInputSurfacesPtr;
        }
    } else {
        if (sharedBuffersPtr != NULL) {
            *sharedBuffersPtr = NULL;
        }
#ifndef ANDROID_1998
        if (vaSurfacesNum == 0) {
            // No need for fake start compress
            mStartCompressDone = true;
        }
#endif
        stopSharedBuffersEncode();
    }
    return status;
#else
    return NO_ERROR;
#endif
}

}
