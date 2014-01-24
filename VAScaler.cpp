/*
 * Copyright (c) 2007-2013 Intel Corporation. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#define LOG_TAG "Camera_VAScaler"
#include <intel_bufmgr.h>
#include <drm_fourcc.h>
#include "LogHelper.h"
#include "VAScaler.h"

#define CHECK_VASTATUS(str) \
    do { \
        if (vaStatus != VA_STATUS_SUCCESS) { \
            LOGE("%s failed :%s\n", str, vaErrorStr(vaStatus)); \
            return UNKNOWN_ERROR;}   \
    }while(0)

namespace android {

/*
 * This structs is copy from graphic area.
 * It's only to get buffer name from buffer handle.
 * Will be remove when buffer handle can be use directly in surface creation
 */
struct mfx_gralloc_drm_handle_t {
    native_handle_t base;
    int magic;

    int width;
    int height;
    int format;
    int usage;

    int name;
    int pid;    // creator

    mutable int other;                                       // registered owner (pid)
    mutable union { int data1; mutable drm_intel_bo *bo; };  // drm buffer object
    union { int data2; uint32_t fb; };                       // framebuffer id
    int pitch;                                               // buffer pitch (in bytes)
    int allocWidth;                                          // Allocated buffer width in pixels.
    int allocHeight;                                         // Allocated buffer height in lines.
};

VAScaler::VAScaler():
    mInitialized(false),
    mVA(NULL),
    mVPP(NULL),
    mIIDKey(0),
    mOIDKey(0),
    mZoomFactor(0)
{
    LOG1("@%s", __FUNCTION__);
    mIBuffers.clear();
    mOBuffers.clear();

    if (init()) {
        LOGE("Fail to initialize VAScaler");
    }
}

VAScaler::~VAScaler()
{
    LOG1("@%s", __FUNCTION__);
    deInit();
}

status_t VAScaler::init()
{
    VAStatus vaStatus;
    LOG1("@%s", __FUNCTION__);

    mVA = new VideoVPPBase();
    if (!mVA) {
        LOGE("Fail to construct VideoVPPBase");
        return NO_MEMORY;
    }

    vaStatus = mVA->start();
    CHECK_VASTATUS("start");

    mVPP = VPParameters::create(mVA);
    if (mVPP == NULL) {
        LOGE("Fail to create VPParameters");
        return UNKNOWN_ERROR;
    }

    return OK;
}

void VAScaler::clearBuffers(KeyedVector<BufferID , RenderTarget *> &buffers)
{
    for (unsigned int i = 0 ; i < buffers.size() ; i++) {
        RenderTarget *rt = buffers.valueAt(i);
        if (rt)
            delete rt;

        buffers.removeItemsAt(i);
    }
}

status_t VAScaler::deInit()
{
    LOG1("@%s", __FUNCTION__);

    //for some reasion of vpp lib. It can not be destructed at the moment.
    //if (mVPP) {
    //    delete mVPP;
    //    mVPP = NULL;
    //}

    if (mVA) {
        mVA->stop();
        delete mVA;
        mVA = NULL;
    }

    if (!mIBuffers.isEmpty()) {
        LOGW("Input buffer is not clear before destory");
        clearBuffers(mIBuffers);
    }

    if (!mOBuffers.isEmpty()) {
        LOGW("Output buffer is not clear before destory");
        clearBuffers(mOBuffers);
    }

    mIIDKey = 0;
    mOIDKey = 0;
    return OK;
}

void VAScaler::setZoomFactor(float zf)
{
    LOG2("@%s setZoomFactor:%f", __FUNCTION__, zf);
    mZoomFactor = zf;
}

void VAScaler::setZoomRegion(VARectangle &region, int w, int h, float zoom)
{
    LOG2("%s %dx%d zoom:%f", __FUNCTION__, w, h, zoom);
    if (zoom == NO_ZOOM) {
        region.x = 0;
        region.y = 0;
        region.width  = w;
        region.height = h;
        return;
    }

    int zw = (int)((float)w/zoom);
    int zh = (int)((float)h/zoom);

    region.width  = ALIGN_WIDTH(zw, 4);
    region.height = ALIGN_WIDTH(zh, 4);

    region.x      = ALIGN_WIDTH(((w-region.width)/2), 4);
    region.y      = ALIGN_WIDTH(((h-region.height)/2), 4);
}

int VAScaler::processFrame(int inputBufferId, int outputBufferId)
{
    VAStatus vaStatus;
    LOG2("@%s in:%d out:%d", __FUNCTION__, inputBufferId, outputBufferId);

    RenderTarget *in  = mIBuffers.valueFor(inputBufferId);
    RenderTarget *out = mOBuffers.valueFor(outputBufferId);

    if (in == NULL || out == NULL) {
        LOGE("Find error render target");
        return -1;
    }

    //correct rect information according to zoom factor
    setZoomRegion(in->rect, in->width, in->height, mZoomFactor);

    vaStatus = mVA->perform(*in, *out, mVPP, false);
    CHECK_VASTATUS("perform");

    return vaStatus;
}

status_t VAScaler::mapGraphicFmtToVAFmt(int &vaRTFormat, int &vaFourcc, int graphicFormat)
{
    LOG1("%s %x", __FUNCTION__, graphicFormat);
    switch (graphicFormat) {
        case HAL_PIXEL_FORMAT_NV12:
            vaRTFormat = VA_RT_FORMAT_YUV420;
            vaFourcc   = VA_FOURCC_NV12;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            vaRTFormat = VA_RT_FORMAT_YUV422;
            vaFourcc   = VA_FOURCC_YUY2;
            break;
        default:
            LOGE("Graphic format:%x is not supported", graphicFormat);
            return BAD_VALUE;
    }

    return OK;
}

int VAScaler::addOutputBuffer(buffer_handle_t *pBufHandle, int width, int height, int stride, int format)
{
    RenderTarget *rt;
    struct mfx_gralloc_drm_handle_t *pGrallocHandle;

    LOG1("@%s %dx%d stride:%d format:%x current count:%d", __FUNCTION__, width, height, stride, format, mOIDKey);
    //double the stride for YUY2
    if (format == HAL_PIXEL_FORMAT_YCbCr_422_I)
        stride *= 2;

    rt = new RenderTarget();
    if (rt == NULL) {
        LOGE("Fail to allocate RenderTarget");
        return -1;
    }

    //FIXME, will be removed when va driver support buffer handle directly.
    pGrallocHandle = (struct mfx_gralloc_drm_handle_t *) *pBufHandle;
    LOG1("info of handle %dx%d stride:%d name:%x format:%x", pGrallocHandle->width, pGrallocHandle->height,
            pGrallocHandle->pitch, pGrallocHandle->name, pGrallocHandle->format);

    rt->width    = width;
    rt->height   = height;
    rt->stride   = pGrallocHandle->pitch;
    rt->type     = RenderTarget::KERNEL_DRM;
    rt->handle   = pGrallocHandle->name;
    rt->rect.x   = rt->rect.y = 0;
    rt->rect.width   = rt->width;
    rt->rect.height  = rt->height;
    mapGraphicFmtToVAFmt(rt->format, rt->pixel_format, format);

    LOG2("addOutputBuffer handle:%x", rt->handle);
    //add to vector
    mOBuffers.add(++mOIDKey, rt);
    return mOIDKey;
}

int VAScaler::addInputBuffer(buffer_handle_t *pBufHandle, int width, int height, int stride, int format)
{
    RenderTarget *rt;
    struct mfx_gralloc_drm_handle_t *pGrallocHandle;

    LOG1("@%s %dx%d stride:%d format:%x current count:%d", __FUNCTION__, width, height, stride, format, mIIDKey);
    //double the stride for YUY2
    if (format == HAL_PIXEL_FORMAT_YCbCr_422_I)
        stride *= 2;

    rt = new RenderTarget();
    if (rt == NULL) {
        LOGE("Fail to allocate RenderTarget");
        return -1;
    }

    pGrallocHandle = (struct mfx_gralloc_drm_handle_t *) *pBufHandle;
    LOG1("info of handle %dx%d stride:%d name:%x format:%x", pGrallocHandle->width, pGrallocHandle->height,
            pGrallocHandle->pitch, pGrallocHandle->name, pGrallocHandle->format);

    rt->width    = width;
    rt->height   = height;
    rt->stride   = pGrallocHandle->pitch;
    rt->type     = RenderTarget::KERNEL_DRM;
    rt->handle   = pGrallocHandle->name;
    rt->rect.x   = rt->rect.y = 0;
    rt->rect.width   = rt->width;
    rt->rect.height  = rt->height;
    mapGraphicFmtToVAFmt(rt->format, rt->pixel_format, format);

    LOG1("addInputBuffer handle:%x", rt->handle);
    //add to vector
    mIBuffers.add(++mIIDKey, rt);
    return mIIDKey;
}

void VAScaler::removeInputBuffer(int bufferId)
{
    LOG1("@%s bufferId:%d", __FUNCTION__ , bufferId);
    RenderTarget *rt = mIBuffers.valueFor(bufferId);
    if (rt) {
        delete rt;
        rt = NULL;
    }

    mIBuffers.removeItem(bufferId);
}

void VAScaler::removeOutputBuffer(int bufferId)
{
    LOG1("@%s bufferId:%d", __FUNCTION__ , bufferId);
    RenderTarget *rt = mOBuffers.valueFor(bufferId);
    if (rt) {
        delete rt;
        rt = NULL;
    }

    mOBuffers.removeItem(bufferId);
}

}; // namespace android
