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
#define LOG_TAG "Camera_CP"

#include <ia_cp.h>
#include "AtomCP.h"
#include "AtomCommon.h"
#include "LogHelper.h"
#include "AtomAcc.h"
#include "PerformanceTraces.h"

namespace android {

extern "C" {
static void vdebug(const char *fmt, va_list ap)
{
    LOG_PRI_VA(ANDROID_LOG_DEBUG, LOG_TAG, fmt, ap);
}

static void verror(const char *fmt, va_list ap)
{
    LOG_PRI_VA(ANDROID_LOG_ERROR, LOG_TAG, fmt, ap);
}

static void vinfo(const char *fmt, va_list ap)
{
    LOG_PRI_VA(ANDROID_LOG_INFO, LOG_TAG, fmt, ap);
}
}

AtomCP::AtomCP(AtomISP *isp)
{
    LOG1("@%s", __FUNCTION__);
    mISP = isp;

    mPrintFunctions.vdebug = vdebug;
    mPrintFunctions.verror = verror;
    mPrintFunctions.vinfo  = vinfo;

    mAccAPI.isp               = isp;
    mAccAPI.open_firmware     = open_firmware;
    mAccAPI.load_firmware     = load_firmware;
    mAccAPI.unload_firmware   = unload_firmware;
    mAccAPI.set_firmware_arg  = set_firmware_arg;
    mAccAPI.start_firmware    = start_firmware;
    mAccAPI.wait_for_firmware = wait_for_firmware;
    mAccAPI.abort_firmware    = abort_firmware;

    /* Differentiate between CSS 1.5 and CSS 1.0.
     * If Acceleration API v1.5 specific functions stay NULL,
     * then Acceleration API v1.0 shall be called. */
    if (isp->getLastDevice() == 3) {
        mAccAPI.map_firmware_arg   = map_firmware_arg;
        mAccAPI.unmap_firmware_arg = unmap_firmware_arg;
        mAccAPI.set_mapped_arg     = set_mapped_arg;
    }
    else {
        mAccAPI.map_firmware_arg   = NULL;
        mAccAPI.unmap_firmware_arg = NULL;
        mAccAPI.set_mapped_arg     = NULL;
    }

    ia_cp_init(&mAccAPI, &mPrintFunctions);
}

AtomCP::~AtomCP()
{
    LOG1("@%s", __FUNCTION__);
    ia_cp_hdr_uninit();
    ia_cp_uninit();
}

status_t AtomCP::computeCDF(const CiUserBuffer& inputBuf, size_t bufIndex)
{
    Mutex::Autolock lock(mLock);
    status_t err = NO_ERROR;
    LOG1("@%s: inputBuf=%p, bufIndex=%u", __FUNCTION__, &inputBuf, bufIndex);

    if (bufIndex > inputBuf.ciBufNum)
        return BAD_VALUE;


    LOG1("Using input CI postview buff %d @%p: (data=%p, size=%d, width=%d, height=%d, format=%d)",
            bufIndex,
            &inputBuf.ciPostviewBuf[bufIndex],
            inputBuf.ciPostviewBuf[bufIndex].data,
            inputBuf.ciPostviewBuf[bufIndex].size,
            inputBuf.ciPostviewBuf[bufIndex].width,
            inputBuf.ciPostviewBuf[bufIndex].height,
            inputBuf.ciPostviewBuf[bufIndex].format);
    if (ia_cp_generate_cdf(&inputBuf.ciPostviewBuf[bufIndex], &inputBuf.hist[bufIndex]) != ia_err_none)
        err = INVALID_OPERATION;
    LOG1("CDF[0..9] obtained: %d %d %d %d %d %d %d %d %d %d",
            *(inputBuf.hist[bufIndex].cdf + 0), *(inputBuf.hist[bufIndex].cdf + 1),
            *(inputBuf.hist[bufIndex].cdf + 2), *(inputBuf.hist[bufIndex].cdf + 3),
            *(inputBuf.hist[bufIndex].cdf + 5), *(inputBuf.hist[bufIndex].cdf + 5),
            *(inputBuf.hist[bufIndex].cdf + 6), *(inputBuf.hist[bufIndex].cdf + 7),
            *(inputBuf.hist[bufIndex].cdf + 8), *(inputBuf.hist[bufIndex].cdf + 9));
    PERFORMANCE_TRACES_BREAKDOWN_STEP("Done");
    return err;
}

status_t AtomCP::composeHDR(const CiUserBuffer& inputBuf, const CiUserBuffer& outputBuf, unsigned vividness, unsigned sharpening)
{
    Mutex::Autolock lock(mLock);
    ia_cp_sharpening ia_sharp;
    ia_cp_vividness ia_vivid;
    ia_err ia_err;

    LOG1("@%s: inputBuf=%p, outputBuf=%p, vividness=%u, sharpening=%u", __FUNCTION__, &inputBuf, &outputBuf, vividness, sharpening);

    switch (vividness) {
    case NO_SHARPENING:
        ia_sharp = ia_cp_sharpening_none;
        break;
    case NORMAL_SHARPENING:
        ia_sharp = ia_cp_sharpening_normal;
        break;
    case STRONG_SHARPENING:
        ia_sharp = ia_cp_sharpening_strong;
        break;
    default:
        return INVALID_OPERATION;
    }

    switch (sharpening) {
    case NO_VIVIDNESS:
        ia_vivid = ia_cp_vividness_none;
        break;
    case GAUSSIAN_VIVIDNESS:
        ia_vivid = ia_cp_vividness_gaussian;
        break;
    case GAMMA_VIVIDNESS:
        ia_vivid = ia_cp_vividness_gamma;
        break;
    default:
        return INVALID_OPERATION;
    }

    ia_err = ia_cp_hdr_compose(outputBuf.ciMainBuf,
                               outputBuf.ciPostviewBuf,
                               inputBuf.ciMainBuf,
                               inputBuf.ciBufNum,
                               ia_sharp,
                               ia_vivid,
                               inputBuf.hist);
    if (ia_err != ia_err_none)
            return INVALID_OPERATION;
    PERFORMANCE_TRACES_BREAKDOWN_STEP_NOPARAM();

    return NO_ERROR;
}

status_t AtomCP::initializeHDR(unsigned width, unsigned height)
{
    LOG1("@%s, size=%ux%u", __FUNCTION__, width, height);
    ia_err ia_err;

    if (mISP->getLastDevice() == 3) {
        ia_err = ia_cp_hdr_init(width, height);
        if (ia_err != ia_err_none)
            return NO_MEMORY;
    }
    PERFORMANCE_TRACES_HDR_SHOT2PREVIEW_CALLED();

    return NO_ERROR;
}

status_t AtomCP::uninitializeHDR(void)
{
    ia_err ia_err;

    if (mISP->getLastDevice() == 3) {
        ia_err = ia_cp_hdr_uninit();
        if (ia_err != ia_err_none)
            return INVALID_OPERATION;
    }
    PERFORMANCE_TRACES_BREAKDOWN_STEP_NOPARAM();

    return NO_ERROR;
}

status_t AtomCP::setIaFrameFormat(ia_frame* iaFrame, int v4l2Format)
{
    LOG1("@%s", __FUNCTION__);
    if (v4l2Format == V4L2_PIX_FMT_YUV420)
        iaFrame->format = ia_frame_format_yuv420;
    else if (v4l2Format == V4L2_PIX_FMT_NV12)
        iaFrame->format = ia_frame_format_nv12;
    else
        return INVALID_OPERATION;

    return NO_ERROR;
}

};
