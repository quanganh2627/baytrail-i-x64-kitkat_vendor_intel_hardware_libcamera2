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

#ifdef ENABLE_INTEL_EXTRAS
AtomCP::AtomCP(HWControlGroup &hwcg) :
    mISP(hwcg.mIspCI)
{
    LOG1("@%s", __FUNCTION__);
    int ispMinor, ispMajor;

    ispMinor = mISP->getCssMinorVersion();
    ispMajor = mISP->getCssMajorVersion();

    mPrintFunctions.vdebug = vdebug;
    mPrintFunctions.verror = verror;
    mPrintFunctions.vinfo  = vinfo;

    mAccAPI.isp               = mISP;
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
    if ((ispMajor*10 + ispMinor) > 10) {
        mAccAPI.map_firmware_arg   = map_firmware_arg;
        mAccAPI.unmap_firmware_arg = unmap_firmware_arg;
        mAccAPI.set_mapped_arg     = set_mapped_arg;
    }
    else {
        mAccAPI.map_firmware_arg   = NULL;
        mAccAPI.unmap_firmware_arg = NULL;
        mAccAPI.set_mapped_arg     = NULL;
    }

    mAccAPI.version_css.major = ispMajor;
    mAccAPI.version_css.minor = ispMinor;
    mAccAPI.version_isp.major = mISP->getIspHwMajorVersion();
    mAccAPI.version_isp.minor = mISP->getIspHwMinorVersion();
    LOG1("@%s: version infor css.major:%d, minor:%d, isp.major:%d, isp.minor:%d", __FUNCTION__,
            mAccAPI.version_css.major, mAccAPI.version_css.minor,
            mAccAPI.version_isp.major, mAccAPI.version_isp.minor);

    ia_cp_init(&mAccAPI, &mPrintFunctions);
}

AtomCP::~AtomCP()
{
    LOG1("@%s", __FUNCTION__);
    ia_cp_hdr_uninit();
    ia_cp_uninit();
}

status_t AtomCP::composeHDR(const CiUserBuffer& inputBuf, const CiUserBuffer& outputBuf, unsigned vividness, unsigned sharpening)
{
    Mutex::Autolock lock(mLock);
    ia_err ia_err;
    ia_cp_hdr_config cfg;

    LOG1("@%s: inputBuf=%p, outputBuf=%p, vividness=%u, sharpening=%u", __FUNCTION__, &inputBuf, &outputBuf, vividness, sharpening);

    ia_cp_hdr_init_config(&cfg);

    ia_err = ia_cp_hdr_compose(outputBuf.ciMainBuf,
                               outputBuf.ciPostviewBuf,
                               inputBuf.ciMainBuf,
                               inputBuf.ciPostviewBuf,
                               inputBuf.ciBufNum,
                               cfg);
    if (ia_err != ia_err_none)
            return INVALID_OPERATION;
    PERFORMANCE_TRACES_HDR_SHOT2PREVIEW_CALLED();
    PERFORMANCE_TRACES_BREAKDOWN_STEP_NOPARAM();

    return NO_ERROR;
}

status_t AtomCP::initializeHDR(unsigned width, unsigned height)
{
    LOG1("@%s, size=%ux%u", __FUNCTION__, width, height);
    ia_err ia_err;

    ia_err = ia_cp_hdr_init(width, height);
    if (ia_err != ia_err_none)
        return NO_MEMORY;

    return NO_ERROR;
}

status_t AtomCP::uninitializeHDR(void)
{
    LOG1("@%s", __FUNCTION__);
    ia_err ia_err;

    ia_err = ia_cp_hdr_uninit();
    if (ia_err != ia_err_none)
        return INVALID_OPERATION;

    PERFORMANCE_TRACES_BREAKDOWN_STEP_NOPARAM();

    return NO_ERROR;
}

status_t AtomCP::setIaFrameFormat(ia_frame* iaFrame, int v4l2Format)
{
    LOG2("@%s", __FUNCTION__);
    switch (v4l2Format) {
    case V4L2_PIX_FMT_YUV420:
        iaFrame->format = ia_frame_format_yuv420;
        break;
    case V4L2_PIX_FMT_NV12:
        iaFrame->format = ia_frame_format_nv12;
        break;
    case V4L2_PIX_FMT_YUYV:
        iaFrame->format = ia_frame_format_yuy2;
        break;
    default:
        return INVALID_OPERATION;
    }

    return NO_ERROR;
}
#endif // ENABLE_INTEL_EXTRAS
}
