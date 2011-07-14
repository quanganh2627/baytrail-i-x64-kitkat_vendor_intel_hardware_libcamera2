/*
**
** Copyright 2008, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#define LOG_TAG "AAAProcess"
#include <utils/Log.h>
#include <math.h>
#include "CameraAAAProcess.h"

namespace android {


AAAProcess::AAAProcess(int sensortype)
    : mGdcEnabled(false),
      mAwbMode(CAM_AWB_MODE_AUTO),
      mAfMode(CAM_AF_MODE_AUTO),
      mSensorType(~0),
      mAfStillFrames(0),
      mInitied(false)
{
    mSensorType = sensortype;
    mAeMode = CAM_AE_MODE_AUTO;
    mFocusPosition = 50;
    mColorTemperature = 5000;
    mManualAperture = 2.8;
    mManualShutter = 1 /60.0;
    mManualIso = 100;
    dvs_vector.x = 0;
    dvs_vector.y = 0;

    //Init();

}

AAAProcess::~AAAProcess()
{
    //Uninit();
}

int AAAProcess::AeLock(bool lock) {
    if(SENSOR_TYPE_RAW == mSensorType)
        return ci_adv_ae_lock(lock);
    else
        return 0;
}

int AAAProcess::AeIsLocked(bool *lock) {
    if(SENSOR_TYPE_RAW == mSensorType)
        return ci_adv_ae_is_locked(lock);
    else
        return 0;
}

void AAAProcess::SetAfEnabled(bool enabled) {
    if(SENSOR_TYPE_RAW == mSensorType)
        ci_adv_af_enable(enabled);
}

void AAAProcess::SetAeEnabled(bool enabled) {
    if(SENSOR_TYPE_RAW == mSensorType)
        ci_adv_ae_enable(enabled);
}

void AAAProcess::SetAwbEnabled(bool enabled) {
    if(SENSOR_TYPE_RAW == mSensorType)
        ci_adv_awb_enable(enabled);
}

void AAAProcess::IspSetFd(int fd)
{
    Mutex::Autolock lock(mLock);
    if(SENSOR_TYPE_RAW == mSensorType)
    {
        if(-1 == fd || !fd)
            ci_adv_isp_set_fd(-1);
        else
            ci_adv_isp_set_fd(fd);

        // fixme, for working around manual focus
        main_fd = fd;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AfApplyResults(void)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_apply_results();
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::SwitchMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_isp_mode isp_mode;
        switch (mode) {
        case PREVIEW_MODE:
            isp_mode = ci_adv_isp_mode_preview;
            break;
        case STILL_IMAGE_MODE:
            isp_mode = ci_adv_isp_mode_capture;
            break;
        case VIDEO_RECORDING_MODE:
            isp_mode = ci_adv_isp_mode_video;
            break;
        default:
            isp_mode = ci_adv_isp_mode_preview;
            LOGW("%s: Wrong mode %d\n", __func__, mode);
            break;
        }
        ci_adv_switch_mode(isp_mode);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::SetFrameRate(float framerate)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_set_frame_rate(CI_ADV_S15_16_FROM_FLOAT(framerate));
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}


void AAAProcess::AeAfAwbProcess(bool read_stats)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_process_frame(read_stats);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AfStillStart(void)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_start();
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AfStillStop(void)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_stop();
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

int AAAProcess::AfStillIsComplete(bool *complete)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        *complete = ci_adv_af_is_complete();
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::PreFlashProcess(cam_flash_stage stage)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_flash_stage wr_stage;
        switch (stage)
        {
        case CAM_FLASH_STAGE_NONE:
            wr_stage = ci_adv_flash_stage_none;
            break;
        case CAM_FLASH_STAGE_PRE:
            wr_stage = ci_adv_flash_stage_pre;
            break;
        case CAM_FLASH_STAGE_MAIN:
            wr_stage = ci_adv_flash_stage_main;
            break;
        default:
            LOGE("not defined flash stage in %s", __func__);
            return AAA_FAIL;

        }
        ci_adv_process_for_flash (wr_stage);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;

}

void AAAProcess::SetStillStabilizationEnabled(bool en)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_dis_enable(en);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::GetStillStabilizationEnabled(bool *en)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_dis_is_enabled(en);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::DisCalcStill(ci_adv_dis_vector *vector, int frame_number)
{
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_dis_calc_still(vector, frame_number);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::StillCompose(ci_adv_user_buffer *com_buf,
                              ci_adv_user_buffer bufs[], int frame_dis,
                              ci_adv_dis_vector vectors[])
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_still_compose(com_buf, bufs, frame_dis, vectors);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::GetDisVector(ci_adv_dis_vector *vector)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_get_dis_vector (vector);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::DoRedeyeRemoval(void *img_buf, int size, int width, int height, int format)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_frame_format out_format;
        switch (format)
        {
        case V4L2_PIX_FMT_YUV420:
            out_format = ci_adv_frame_format_yuv420;
            break;
        default:
            LOGE("%s: not supported foramt in red eye removal",  __func__);
            return;
        }
        ci_adv_correct_redeyes(img_buf, size, width, height, out_format);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::LoadGdcTable(void)
{
    if(!mInitied)
        return;

    if(!mGdcEnabled)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_load_gdc_table();
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

int AAAProcess::AeSetMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_mode wr_val;
        switch (mode)
        {
        case CAM_AE_MODE_AUTO:
            wr_val = ci_adv_ae_mode_auto;
            break;
        case CAM_AE_MODE_MANUAL:
            wr_val = ci_adv_ae_mode_manual;
            break;
        case CAM_AE_MODE_SHUTTER_PRIORITY:
            wr_val = ci_adv_ae_mode_shutter_priority;
            break;
        case CAM_AE_MODE_APERTURE_PRIORITY:
            wr_val = ci_adv_ae_mode_aperture_priority;
            break;
         default:
            LOGE("%s: set invalid AE mode\n", __func__);
            wr_val = ci_adv_ae_mode_auto;
        }
        ci_adv_err ret = ci_adv_ae_set_mode(wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        mAeMode = mode;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetMode(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_mode rd_val;
        ci_adv_err ret = ci_adv_ae_get_mode(&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_ae_mode_auto:
            *mode = CAM_AE_MODE_AUTO;
            break;
        case ci_adv_ae_mode_manual:
            *mode = CAM_AE_MODE_MANUAL;
            break;
        case ci_adv_ae_mode_shutter_priority:
            *mode = CAM_AE_MODE_SHUTTER_PRIORITY;
            break;
        case ci_adv_ae_mode_aperture_priority:
            *mode = CAM_AE_MODE_APERTURE_PRIORITY;
            break;
        default:
            LOGE("%s: get invalid AE mode\n", __func__);
            *mode = CAM_AE_MODE_AUTO;
        }
        mAeMode = *mode;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetMeteringMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_metering_mode wr_val;
        switch (mode)
        {
        case CAM_AE_METERING_MODE_SPOT:
            wr_val = ci_adv_ae_metering_mode_spot;
            break;
        case CAM_AE_METERING_MODE_CENTER:
            wr_val = ci_adv_ae_metering_mode_center;
            break;
        case CAM_AE_METERING_MODE_CUSTOMIZED:
            wr_val = ci_adv_ae_metering_mode_customized;
            break;
        case CAM_AE_METERING_MODE_AUTO:
            wr_val = ci_adv_ae_metering_mode_auto;
            break;
        default:
            LOGE("%s: set invalid AE meter mode\n", __func__);
            wr_val = ci_adv_ae_metering_mode_auto;
        }
        ci_adv_err ret = ci_adv_ae_set_metering_mode(wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetMeteringMode(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_metering_mode rd_val;
        ci_adv_err ret = ci_adv_ae_get_metering_mode(&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_ae_metering_mode_spot:
            *mode = CAM_AE_METERING_MODE_SPOT;
            break;
        case ci_adv_ae_metering_mode_center:
            *mode = CAM_AE_METERING_MODE_CENTER;
            break;
        case ci_adv_ae_metering_mode_customized:
            *mode = CAM_AE_METERING_MODE_CUSTOMIZED;
            break;
        case ci_adv_ae_metering_mode_auto:
            *mode = CAM_AE_METERING_MODE_AUTO;
            break;
         default:
            LOGE("%s: get invalid AE meter mode", __func__);
            *mode = CAM_AE_METERING_MODE_AUTO;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetEv(float bias)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        bias = bias > 2 ? 2 : bias;
        bias = bias < -2 ? -2 : bias;
        ci_adv_err ret = ci_adv_ae_set_bias(CI_ADV_S15_16_FROM_FLOAT(bias));
        if(ci_adv_success != ret)
        {
            LOGE("!!!line:%d, in AeSetEv, ret:%d\n", __LINE__, ret);
            return AAA_FAIL;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetEv(float *bias)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        int ibias;
        ci_adv_err ret = ci_adv_ae_get_bias(&ibias);
        *bias = CI_ADV_S15_16_TO_FLOAT(ibias);
        if(ci_adv_success != ret)
        {
            LOGE("!!!line:%d, in AeGetEv, ret:%d\n", __LINE__, ret);
            return AAA_FAIL;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetSceneMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_exposure_program wr_val;
        switch (mode) {
        case CAM_AE_SCENE_MODE_AUTO:
            wr_val = ci_adv_ae_exposure_program_auto;
            break;
        case CAM_AE_SCENE_MODE_PORTRAIT:
            wr_val = ci_adv_ae_exposure_program_portrait;
            break;
        case CAM_AE_SCENE_MODE_SPORTS:
            wr_val = ci_adv_ae_exposure_program_sports;
            break;
        case CAM_AE_SCENE_MODE_LANDSCAPE:
            wr_val = ci_adv_ae_exposure_program_landscape;
            break;
        case CAM_AE_SCENE_MODE_NIGHT:
            wr_val = ci_adv_ae_exposure_program_night;
            break;
        case CAM_AE_SCENE_MODE_FIREWORKS:
            wr_val = ci_adv_ae_exposure_program_fireworks;
            break;
        default:
            LOGE("%s: set invalid AE scene mode\n", __func__);
            wr_val = ci_adv_ae_exposure_program_auto;
        }
        ci_adv_err ret = ci_adv_ae_set_exposure_program (wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetSceneMode(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_exposure_program rd_val;
        ci_adv_err ret = ci_adv_ae_get_exposure_program (&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val) {
        case ci_adv_ae_exposure_program_auto:
            *mode = CAM_AE_SCENE_MODE_AUTO;
            break;
        case ci_adv_ae_exposure_program_portrait:
            *mode = CAM_AE_SCENE_MODE_PORTRAIT;
            break;
        case ci_adv_ae_exposure_program_sports:
            *mode = CAM_AE_SCENE_MODE_SPORTS;
            break;
        case ci_adv_ae_exposure_program_landscape:
            *mode = CAM_AE_SCENE_MODE_LANDSCAPE;
            break;
        case ci_adv_ae_exposure_program_night:
            *mode = CAM_AE_SCENE_MODE_NIGHT;
            break;
        case ci_adv_ae_exposure_program_fireworks:
            *mode = CAM_AE_SCENE_MODE_FIREWORKS;
            break;
        default:
            LOGE("%s: get invalid AE scene mode\n", __func__);
            *mode = CAM_AE_SCENE_MODE_AUTO;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetFlashMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_flash_mode wr_val;
        switch (mode) {
        case CAM_AE_FLASH_MODE_AUTO:
            wr_val = ci_adv_ae_flash_mode_auto;
            break;
        case CAM_AE_FLASH_MODE_OFF:
            wr_val = ci_adv_ae_flash_mode_off;
            break;
        case CAM_AE_FLASH_MODE_ON:
            wr_val = ci_adv_ae_flash_mode_on;
            break;
        case CAM_AE_FLASH_MODE_DAY_SYNC:
            wr_val = ci_adv_ae_flash_mode_day_sync;
            break;
        case CAM_AE_FLASH_MODE_SLOW_SYNC:
            wr_val = ci_adv_ae_flash_mode_slow_sync;
            break;
        default:
            LOGE("%s: set invalid flash mode\n", __func__);
            wr_val = ci_adv_ae_flash_mode_auto;
        }
        ci_adv_err ret = ci_adv_ae_set_flash_mode(wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetFlashMode(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_flash_mode rd_val;
        ci_adv_err ret = ci_adv_ae_get_flash_mode(&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val) {
        case ci_adv_ae_flash_mode_auto:
            *mode = CAM_AE_FLASH_MODE_AUTO;
            break;
        case ci_adv_ae_flash_mode_off:
            *mode = CAM_AE_FLASH_MODE_OFF;
            break;
        case ci_adv_ae_flash_mode_on:
            *mode = CAM_AE_FLASH_MODE_ON;
            break;
        case ci_adv_ae_flash_mode_day_sync:
            *mode = CAM_AE_FLASH_MODE_DAY_SYNC;
            break;
        case ci_adv_ae_flash_mode_slow_sync:
            *mode = CAM_AE_FLASH_MODE_SLOW_SYNC;
            break;
        default:
            LOGE("%s: get invalid flash mode\n", __func__);
            *mode = CAM_AE_FLASH_MODE_AUTO;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeIsFlashNecessary(bool *used)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    *used = false;
    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_err ret = ci_adv_ae_is_flash_necessary(used);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetFlickerMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_flicker_mode wr_val;
        switch (mode)
        {
        case CAM_AE_FLICKER_MODE_OFF:
            wr_val = ci_adv_ae_flicker_mode_off;
            break;
        case CAM_AE_FLICKER_MODE_50HZ:
            wr_val = ci_adv_ae_flicker_mode_50hz;
            break;
        case CAM_AE_FLICKER_MODE_60HZ:
            wr_val = ci_adv_ae_flicker_mode_60hz;
            break;
        case CAM_AE_FLICKER_MODE_AUTO:
            wr_val = ci_adv_ae_flicker_mode_auto;
            break;
        default:
            LOGE("%s: set invalid flicker mode\n", __func__);
            wr_val = ci_adv_ae_flicker_mode_auto;
        }
        ci_adv_err ret = ci_adv_ae_set_flicker_mode(wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetFlickerMode(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_flicker_mode rd_val;
        ci_adv_err ret = ci_adv_ae_get_flicker_mode(&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_ae_flicker_mode_off:
            *mode = CAM_AE_FLICKER_MODE_OFF;
            break;
        case ci_adv_ae_flicker_mode_50hz:
            *mode = CAM_AE_FLICKER_MODE_50HZ;
            break;
        case ci_adv_ae_flicker_mode_60hz:
            *mode = CAM_AE_FLICKER_MODE_60HZ;
            break;
        case ci_adv_ae_flicker_mode_auto:
            *mode = CAM_AE_FLICKER_MODE_AUTO;
            break;
        default:
            LOGE("%s: get invalid flicker mode\n", __func__);
            *mode = CAM_AE_FLICKER_MODE_AUTO;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetManualIso(int sensitivity, bool to_hw)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        float fev;
        if (sensitivity <=0)
        {
            LOGE("error in get log2 math computation in line %d\n", __LINE__);
            return AAA_FAIL;
        }

        mManualIso = sensitivity;

        if(to_hw)
        {
            fev = log10((float)sensitivity / 3.125) / log10(2.0);
            ci_adv_err ret = ci_adv_ae_set_manual_iso(CI_ADV_S15_16_FROM_FLOAT(fev));
            if(ci_adv_success != ret)
                return AAA_FAIL;

            LOGD(" *** manual set iso in EV: %f\n", fev);
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetManualIso(int *sensitivity)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        int iev;
        ci_adv_err ret = ci_adv_ae_get_manual_iso(&iev);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        *sensitivity = (int)(3.125 * pow(2, CI_ADV_S15_16_TO_FLOAT(iev)));
        mManualIso = *sensitivity;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetManualAperture(float aperture, bool to_hw)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        float fev;
        if (aperture <=0)
        {
            LOGE("error in get log2 math computation in line %d\n", __LINE__);
            return AAA_FAIL;
        }

        mManualAperture = aperture;

        if (to_hw)
        {
            fev = 2.0 * (log10(aperture) / log10(2.0));
            ci_adv_err ret = ci_adv_ae_set_manual_aperture(CI_ADV_S15_16_FROM_FLOAT(fev));
            if(ci_adv_success != ret)
                return AAA_FAIL;

            LOGD(" *** manual set aperture in EV: %f\n", fev);
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetManualAperture(float *aperture)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        int iev;
        ci_adv_err ret = ci_adv_ae_get_manual_aperture(&iev);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        *aperture = pow(2, CI_ADV_S15_16_TO_FLOAT(iev) / 2.0);
        mManualAperture = *aperture;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetManualBrightness(float *brightness)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        int val;
        ci_adv_err ret = ci_adv_ae_get_manual_brightness(&val);
        if (ci_adv_success != ret)
            return AAA_FAIL;

        *brightness = (float)((float)val / 65536.0);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetManualShutter(float exp_time, bool to_hw)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        float fev;
        if (exp_time <=0)
        {
            LOGE("error in get log2 math computation in line %d\n", __LINE__);
            return AAA_FAIL;
        }

        mManualShutter = exp_time;

        if (to_hw)
        {
            fev = -1.0 * (log10(exp_time) / log10(2.0));
            ci_adv_err ret = ci_adv_ae_set_manual_shutter(CI_ADV_S15_16_FROM_FLOAT(fev));
            if(ci_adv_success != ret)
                return AAA_FAIL;

            LOGD(" *** manual set shutter in EV: %f\n", fev);
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetManualShutter(float *exp_time)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        int iev;
        ci_adv_err ret = ci_adv_ae_get_manual_shutter(&iev);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        *exp_time = pow(2, -1.0 * CI_ADV_S15_16_TO_FLOAT(iev));
        mManualShutter = *exp_time;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfSetManualFocus(int focus, bool to_hw)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        mFocusPosition = focus;

        if (to_hw)
        {

        //fixme: use distance as manual focus control
        //int ret = ci_adv_af_manual_focus_abs(focus);
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;
        controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
        controls.count = 1;
        controls.controls = &control;
        control.id = V4L2_CID_FOCUS_ABSOLUTE;
        control.value = focus;
        int ret = ioctl (main_fd, VIDIOC_S_EXT_CTRLS, &controls);

            if(0 != ret)
                return AAA_FAIL;
        }

        LOGD(" *** manual set focus distance in cm: %d\n", focus);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;

}

int AAAProcess::AfGetManualFocus(int *focus)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        *focus = mFocusPosition;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;

}

int AAAProcess::AfGetFocus(int *focus)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        struct v4l2_ext_controls controls;
        struct v4l2_ext_control control;

        controls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
        controls.count = 1;
        controls.controls = &control;
        control.id = V4L2_CID_FOCUS_ABSOLUTE;
        int ret = ioctl (main_fd, VIDIOC_S_EXT_CTRLS, &controls);
        LOG1("line:%d, ret:%d, focus:%d", __LINE__, ret, *focus);
        *focus = control.value;

    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetWindow(const cam_Window *window)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_err ret = ci_adv_ae_set_window((ci_adv_window *)window);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetWindow(cam_Window *window)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_err ret = ci_adv_ae_get_window((ci_adv_window *)window);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbSetMode (int wb_mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_err ret = ci_adv_success;
        switch (wb_mode) {
        case CAM_AWB_MODE_DAYLIGHT:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_clear_sky);
            break;
        case CAM_AWB_MODE_CLOUDY:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_cloudiness);
            break;
        case CAM_AWB_MODE_SUNSET:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_filament_lamp);
            break;
        case CAM_AWB_MODE_TUNGSTEN:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_filament_lamp);
            break;
        case CAM_AWB_MODE_FLUORESCENT:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_fluorlamp_n);
            break;
        case CAM_AWB_MODE_WARM_FLUORESCENT:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_fluorlamp_w);
            break;
        case CAM_AWB_MODE_WARM_INCANDESCENT:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_filament_lamp);
            break;
        case CAM_AWB_MODE_SHADOW:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            ret = ci_adv_awb_set_light_source (ci_adv_awb_light_source_shadow_area);
            break;
        case CAM_AWB_MODE_MANUAL_INPUT:
            ci_adv_awb_set_mode (ci_adv_awb_mode_manual);
            break;
        case CAM_AWB_MODE_AUTO:
            ret = ci_adv_awb_set_mode (ci_adv_awb_mode_auto);
            break;
        default:
            LOGE("%s: set invalid AWB mode\n", __func__);
            ret = ci_adv_awb_set_mode (ci_adv_awb_mode_auto);
        }
        if (ret != ci_adv_success)
            return AAA_FAIL;
        mAwbMode = wb_mode;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbGetMode(int *wb_mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        *wb_mode = mAwbMode;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbSetManualColorTemperature(int ct, bool to_hw)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        mColorTemperature = ct;

        if (to_hw)
        {
            ci_adv_err ret = ci_adv_awb_set_manual_color_temperature(ct);
            if(ci_adv_success != ret)
                return AAA_FAIL;
        }
        LOGD(" *** manual set color temperture in Kelvin: %d\n", ct);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbGetManualColorTemperature(int *ct)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        *ct = mColorTemperature;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetBacklightCorrection(bool en)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_backlight_correction_mode wr_val;
        if (en == true)
        {
            wr_val = ci_adv_ae_backlight_correction_mode_on;
        }
        else
        {
            wr_val = ci_adv_ae_backlight_correction_mode_off;
        }
        ci_adv_err ret = ci_adv_ae_set_backlight_correction (wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetBacklightCorrection(bool *en)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_backlight_correction_mode rd_val;
        ci_adv_err ret = ci_adv_ae_get_backlight_correction(&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_ae_backlight_correction_mode_off:
            *en = false;
            break;
        case ci_adv_ae_backlight_correction_mode_on:
            *en = true;
            break;
        default:
            LOGE("%s: get invalid AE backlight correction \n", __func__);
            *en = false;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetExpCfg(unsigned short * exp_time,
                                                                    unsigned short * iso_speed,
                                                                    unsigned short * ss_exp_time,
                                                                    unsigned short * ss_iso_speed,
                                                                    unsigned short * aperture)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_get_exp_cfg(exp_time, iso_speed, ss_exp_time, ss_iso_speed, aperture);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::SetRedEyeRemoval(bool en)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
       return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_redeye_enable(en);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::GetRedEyeRemoval(bool *en)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_redeye_is_enabled (en);
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbSetMapping(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_awb_map wr_val;
        switch (mode)
        {
        case CAM_AWB_MAP_INDOOR:
            wr_val = ci_adv_awb_map_indoor;
            break;
        case CAM_AWB_MAP_OUTDOOR:
            wr_val = ci_adv_awb_map_outdoor;
            break;
        default:
            LOGE("%s: set invalid AWB map mode\n", __func__);
            wr_val = ci_adv_awb_map_indoor;
        }
        ci_adv_err ret = ci_adv_awb_set_map (wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbGetMapping(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_awb_map rd_val;
        ci_adv_err ret = ci_adv_awb_get_map (&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_awb_map_indoor:
            *mode = CAM_AWB_MAP_INDOOR;
            break;
        case ci_adv_awb_map_outdoor:
            *mode = CAM_AWB_MAP_OUTDOOR;
            break;
        default:
            LOGE("%s: get invalid AWB map mode\n", __func__);
            *mode = CAM_AWB_MAP_INDOOR;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;

}

int AAAProcess::AfSetMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_err ret = ci_adv_success;

        switch (mode) {
        case CAM_AF_MODE_AUTO:
        case CAM_AF_MODE_TOUCH:
            ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
            ci_adv_af_set_range (ci_adv_af_range_norm);
            break;
        case CAM_AF_MODE_MACRO:
            ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
            ci_adv_af_set_range (ci_adv_af_range_macro);
            break;
        case CAM_AF_MODE_INFINITY:
            ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
            ci_adv_af_set_range (ci_adv_af_range_full);
            break;
        case CAM_AF_MODE_MANUAL:
            ret = ci_adv_af_set_mode (ci_adv_af_mode_manual);
            ci_adv_af_set_range (ci_adv_af_range_full);
            break;
        default:
            LOGE("%s: set invalid AF mode\n", __func__);
            ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
            ci_adv_af_set_range (ci_adv_af_range_norm);
            break;
        }
        if (ret != ci_adv_success)
            return AAA_FAIL;
        mAfMode = mode;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfGetMode(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        *mode = mAfMode;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfSetMeteringMode(int mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_metering_mode wr_val;
        switch (mode)
        {
        case CAM_AF_METERING_MODE_AUTO:
            wr_val = ci_adv_af_metering_mode_auto;
            break;
        case CAM_AF_METERING_MODE_SPOT:
            wr_val = ci_adv_af_metering_mode_spot;
            break;
        default:
            LOGE("%s: set invalid AF meter mode\n", __func__);
            wr_val = ci_adv_af_metering_mode_auto;
        }
        ci_adv_err ret = ci_adv_af_set_metering_mode(wr_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfGetMeteringMode(int *mode)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_metering_mode rd_val;
        ci_adv_err ret = ci_adv_af_get_metering_mode(&rd_val);
        if(ci_adv_success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_af_metering_mode_auto:
            *mode = CAM_AF_METERING_MODE_AUTO;
            break;
        case ci_adv_af_metering_mode_spot:
            *mode = CAM_AF_METERING_MODE_SPOT;
            break;
        default:
            LOGE("%s: get invalid AF meter mode\n", __func__);
            *mode = CAM_AF_METERING_MODE_AUTO;
        }
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfSetWindow(const cam_Window *window)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_err ret = ci_adv_af_set_window((ci_adv_window *)window);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfGetWindow(cam_Window *window)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return AAA_FAIL;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_err ret = ci_adv_af_get_window((ci_adv_window *)window);
        if(ci_adv_success != ret)
            return AAA_FAIL;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::FlushManualSettings(void)
{
    int ret;

    // manual focus
    if (mAfMode == CAM_AF_MODE_MANUAL)
    {
        ret = AfSetManualFocus (mFocusPosition, true);

        if (ret != AAA_SUCCESS)
        {
            LOGE(" error in flush manual focus\n");
            return AAA_FAIL;
        }
    }

    return AAA_SUCCESS;
}

/* private interface */
void AAAProcess::Init(int sensor)
{
    Mutex::Autolock lock(mLock);
    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_init(sensor);
        mInitied = 1;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::Uninit(void)
{
    Mutex::Autolock lock(mLock);
    if(!mInitied)
        return;

    if(SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_uninit();
        mInitied = 0;
    }
    else if(SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

}; // namespace android
