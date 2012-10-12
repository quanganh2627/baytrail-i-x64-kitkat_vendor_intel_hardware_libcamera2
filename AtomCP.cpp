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

static int get_file_size (FILE *file)
{
    int len;

    if (file == NULL) return 0;

    if (fseek(file, 0, SEEK_END)) return 0;

    len = ftell(file);

    if (fseek(file, 0, SEEK_SET)) return 0;

    return len;
}

static void *open_firmware(const char *fw_path, unsigned *size)
{
    FILE *file;
    unsigned len;
    void *fw;

    LOG1("@%s", __FUNCTION__);
    if (!fw_path)
        return NULL;

    file = fopen(fw_path, "rb");
    if (!file)
        return NULL;

    len = get_file_size(file);

    if (!len) {
        fclose(file);
        return NULL;
    }

    fw = malloc(len);
    if (!fw) {
        fclose(file);
        return NULL;
    }

    if (fread(fw, 1, len, file) != len) {
        fclose(file);
        free(fw);
        return NULL;
    }

    *size = len;

    fclose(file);

    return fw;
}

static int load_firmware(void *isp, void *fw, unsigned size, unsigned *handle)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->loadAccFirmware(fw, size, handle);
}

static int unload_firmware(void *isp, unsigned handle)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->unloadAccFirmware(handle);
}

static int map_firmware_arg (void *isp, void *val, size_t size, unsigned long *ptr)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->mapFirmwareArgument(val, size, ptr);
}

static int unmap_firmware_arg (void *isp, unsigned long val, size_t size)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->unmapFirmwareArgument(val, size);
}

static int set_firmware_arg(void *isp, unsigned handle, unsigned num, void *val, size_t size)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->setFirmwareArgument(handle, num, val, size);
}

static int set_mapped_arg(void *isp, unsigned handle, unsigned mem, unsigned long val, size_t size)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->setMappedFirmwareArgument(handle, mem, val, size);
}

static int start_firmware(void *isp, unsigned handle)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->startFirmware(handle);
}

static int wait_for_firmware(void *isp, unsigned handle)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->waitForFirmware(handle);
}

static int abort_firmware(void *isp, unsigned handle, unsigned timeout)
{
    AtomISP *ISP = (AtomISP*)isp;
    LOG1("@%s", __FUNCTION__);
    return ISP->abortFirmware(handle, timeout);
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
