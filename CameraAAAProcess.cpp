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


AAAProcess::AAAProcess(unsigned int sensortype)
    : mAeEnabled(false),
      mAfEnabled(false),
      mAwbEnabled(false),
      mRedEyeRemovalEnabled(false),
      mStillStabilizationEnabled(false),
      mGdcEnabled(false),
      mAwbMode(CAM_AWB_MODE_AUTO),
      mAfMode(CAM_AF_MODE_AUTO),
      mSensorType(~0),
      mAfStillFrames(0),
      mInitied(false)
{
    mSensorType = sensortype;
    mAwbFlashEnabled = false;
    mAeFlashEnabled = false;
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

void AAAProcess::IspSetFd(int fd)
{
    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        if(-1 == fd || !fd)
            ci_adv_isp_set_fd(-1);
        else
            ci_adv_isp_set_fd(fd);

        // fixme, for working around manual focus
        main_fd = fd;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

/* public interface */
void AAAProcess::AfProcess(void)
{
    if(!mInitied)
        return;

    if(!mAfEnabled && !mAfStillEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_process();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AeProcess(void)
{
    if(!mInitied)
        return;

    if(!mAeEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_process();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AwbProcess(void)
{
    if(!mInitied)
        return;

    if(!mAwbEnabled && !mAwbFlashEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_awb_process();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::GetStatistics(void)
{
    if(!mInitied)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_get_statistics();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AeApplyResults(void)
{
    if(!mInitied)
        return;

    if(!mAeEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_apply_results();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AwbApplyResults(void)
{
    if(!mInitied)
        return;

    if(!mAwbEnabled && !mAwbFlashEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_awb_apply_results();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AfApplyResults(void)
{
    if(!mInitied)
        return;

    if(!mAfEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_apply_results();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

int AAAProcess::ModeSpecInit(void)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        int ret = ci_adv_mode_spec_init();
        if(ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

void AAAProcess::SwitchMode(int mode, int frm_rt)
{
    if(!mInitied)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        CI_ISP_MODE isp_mode;
        switch (mode) {
        case PREVIEW_MODE:
            isp_mode = CI_ISP_MODE_PREVIEW;
            break;
        case STILL_IMAGE_MODE:
            isp_mode = CI_ISP_MODE_CAPTURE;
            break;
        case VIDEO_RECORDING_MODE:
            isp_mode = CI_ISP_MODE_VIDEO;
            break;
        default:
            isp_mode = CI_ISP_MODE_PREVIEW;
            LOGW("%s: Wrong mode %d\n", __func__, mode);
            break;
        }
        ci_adv_switch_mode(isp_mode, frm_rt);
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AfStillStart(void)
{
    if(!mInitied)
        return;

    if(!mAfStillEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_start();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::AfStillStop(void)
{
    if(!mInitied)
        return;

    if(!mAfStillEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_af_stop();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

int AAAProcess::AfStillIsComplete(bool *complete)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAfStillEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        *complete = ci_adv_af_is_complete();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeCalcForFlash(void)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeFlashEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_calc_for_flash ();

    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeCalcWithoutFlash(void)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeFlashEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_calc_without_flash ();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeCalcWithFlash(void)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeFlashEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_ae_calc_with_flash ();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbCalcFlash(void)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAwbFlashEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_awb_calc_flash ();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;

}

void AAAProcess::DisReadStatistics(void)
{
    if(!mInitied)
        return;

    if(!mStillStabilizationEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_dis_read_statistics();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::UpdateDisResults(void)
{
    if(!mInitied)
        return;

    if(!mStillStabilizationEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_update_dis_results();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::DisProcess(ci_adv_dis_vector *dis_vector)
{
    if(!mInitied)
        return;

    if(!mStillStabilizationEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_dis_process(dis_vector);
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::DisCalcStill(ci_adv_dis_vector *vector, int frame_number)
{
    if(!mInitied)
        return;

    if(!mStillStabilizationEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_dis_calc_still(vector, frame_number);
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::StillCompose(struct user_buffer *com_buf,
                              struct user_buffer bufs[], int frame_dis, ci_adv_dis_vector vectors[])
{
    if(!mInitied)
        return;

    if(!mStillStabilizationEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_still_compose(com_buf, bufs, frame_dis, vectors);
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::DoRedeyeRemoval(void *img_buf, int size, int width, int height, int format)
{
    if(!mInitied)
        return;

    if(!mRedEyeRemovalEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_FrameFormat out_format;
        switch (format)
        {
        case V4L2_PIX_FMT_YUV420:
            out_format = ci_adv_FrameFormat_YUV420;
            break;
        default:
            LOGE("%s: not supported foramt in red eye removal",  __func__);
            return;
        }
        ci_adv_do_redeye_removal(img_buf, size, width, height, out_format);
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::LoadGdcTable(void)
{
    if(!mInitied)
        return;

    if(!mGdcEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_load_gdc_table();
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

int AAAProcess::AeSetMode(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeMode wr_val;
        switch (mode)
        {
        case CAM_AE_MODE_AUTO:
            wr_val = ci_adv_AeMode_Auto;
            break;
        case CAM_AE_MODE_MANUAL:
            wr_val = ci_adv_AeMode_Manual;
            break;
        case CAM_AE_MODE_SHUTTER_PRIORITY:
            wr_val = ci_adv_AeMode_ShutterPriority;
            break;
        case CAM_AE_MODE_APERTURE_PRIORITY:
            wr_val = ci_adv_AeMode_AperturePriority;
            break;
         default:
            LOGE("%s: set invalid AE mode\n", __func__);
            wr_val = ci_adv_AeMode_Auto;
        }
        ci_adv_Err ret = ci_adv_AeSetMode(wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        mAeMode = mode;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetMode(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeMode rd_val;
        ci_adv_Err ret = ci_adv_AeGetMode(&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_AeMode_Auto:
            *mode = CAM_AE_MODE_AUTO;
            break;
        case ci_adv_AeMode_Manual:
            *mode = CAM_AE_MODE_MANUAL;
            break;
        case ci_adv_AeMode_ShutterPriority:
            *mode = CAM_AE_MODE_SHUTTER_PRIORITY;
            break;
        case ci_adv_AeMode_AperturePriority:
            *mode = CAM_AE_MODE_APERTURE_PRIORITY;
            break;
        default:
            LOGE("%s: get invalid AE mode\n", __func__);
            *mode = CAM_AE_MODE_AUTO;
        }
        mAeMode = *mode;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetMeteringMode(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeMeteringMode wr_val;
        switch (mode)
        {
        case CAM_AE_METERING_MODE_SPOT:
            wr_val = ci_adv_AeMeteringMode_Spot;
            break;
        case CAM_AE_METERING_MODE_CENTER:
            wr_val = ci_adv_AeMeteringMode_Center;
            break;
        case CAM_AE_METERING_MODE_CUSTOMIZED:
            wr_val = ci_adv_AeMeteringMode_Customized;
            break;
        case CAM_AE_METERING_MODE_AUTO:
            wr_val = ci_adv_AeMeteringMode_Auto;
            break;
        default:
            LOGE("%s: set invalid AE meter mode\n", __func__);
            wr_val = ci_adv_AeMeteringMode_Auto;
        }
        ci_adv_Err ret = ci_adv_AeSetMeteringMode(wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetMeteringMode(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeMeteringMode rd_val;
        ci_adv_Err ret = ci_adv_AeGetMeteringMode(&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_AeMeteringMode_Spot:
            *mode = CAM_AE_METERING_MODE_SPOT;
            break;
        case ci_adv_AeMeteringMode_Center:
            *mode = CAM_AE_METERING_MODE_CENTER;
            break;
        case ci_adv_AeMeteringMode_Customized:
            *mode = CAM_AE_METERING_MODE_CUSTOMIZED;
            break;
        case ci_adv_AeMeteringMode_Auto:
            *mode = CAM_AE_METERING_MODE_AUTO;
            break;
         default:
            LOGE("%s: get invalid AE meter mode", __func__);
            *mode = CAM_AE_METERING_MODE_AUTO;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetEv(float bias)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        bias = bias > 2 ? 2 : bias;
        bias = bias < -2 ? -2 : bias;
        ci_adv_Err ret = ci_adv_AeSetBias((int)(bias * 65536));
        if(ci_adv_Success != ret)
        {
            LOGE("!!!line:%d, in AeSetEv, ret:%d\n", __LINE__, ret);
            return AAA_FAIL;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetEv(float *bias)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        int ibias;
        ci_adv_Err ret = ci_adv_AeGetBias(&ibias);
        *bias = (float) ibias / 65536.0;
        if(ci_adv_Success != ret)
        {
            LOGE("!!!line:%d, in AeGetEv, ret:%d\n", __LINE__, ret);
            return AAA_FAIL;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetSceneMode(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeExposureProgram wr_val;
        switch (mode) {
        case CAM_AE_SCENE_MODE_AUTO:
            wr_val = ci_adv_AeExposureProgram_Auto;
            break;
        case CAM_AE_SCENE_MODE_PORTRAIT:
            wr_val = ci_adv_AeExposureProgram_Portrait;
            break;
        case CAM_AE_SCENE_MODE_SPORTS:
            wr_val = ci_adv_AeExposureProgram_Sports;
            break;
        case CAM_AE_SCENE_MODE_LANDSCAPE:
            wr_val = ci_adv_AeExposureProgram_Landscape;
            break;
        case CAM_AE_SCENE_MODE_NIGHT:
            wr_val = ci_adv_AeExposureProgram_Night;
            break;
        case CAM_AE_SCENE_MODE_FIREWORKS:
            wr_val = ci_adv_AeExposureProgram_Fireworks;
            break;
        default:
            LOGE("%s: set invalid AE scene mode\n", __func__);
            wr_val = ci_adv_AeExposureProgram_Auto;
        }
        ci_adv_Err ret = ci_adv_AeSetExposureProgram (wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetSceneMode(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeExposureProgram rd_val;
        ci_adv_Err ret = ci_adv_AeGetExposureProgram (&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val) {
        case ci_adv_AeExposureProgram_Auto:
            *mode = CAM_AE_SCENE_MODE_AUTO;
            break;
        case ci_adv_AeExposureProgram_Portrait:
            *mode = CAM_AE_SCENE_MODE_PORTRAIT;
            break;
        case ci_adv_AeExposureProgram_Sports:
            *mode = CAM_AE_SCENE_MODE_SPORTS;
            break;
        case ci_adv_AeExposureProgram_Landscape:
            *mode = CAM_AE_SCENE_MODE_LANDSCAPE;
            break;
        case ci_adv_AeExposureProgram_Night:
            *mode = CAM_AE_SCENE_MODE_NIGHT;
            break;
        case ci_adv_AeExposureProgram_Fireworks:
            *mode = CAM_AE_SCENE_MODE_FIREWORKS;
            break;
        default:
            LOGE("%s: get invalid AE scene mode\n", __func__);
            *mode = CAM_AE_SCENE_MODE_AUTO;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetFlashMode(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeFlashMode wr_val;
        switch (mode) {
        case CAM_AE_FLASH_MODE_AUTO:
            wr_val = ci_adv_AeFlashMode_Auto;
            break;
        case CAM_AE_FLASH_MODE_OFF:
            wr_val = ci_adv_AeFlashMode_Off;
            break;
        case CAM_AE_FLASH_MODE_ON:
            wr_val = ci_adv_AeFlashMode_On;
            break;
        case CAM_AE_FLASH_MODE_DAY_SYNC:
            wr_val = ci_adv_AeFlashMode_DaySync;
            break;
        case CAM_AE_FLASH_MODE_SLOW_SYNC:
            wr_val = ci_adv_AeFlashMode_SlowSync;
            break;
        default:
            LOGE("%s: set invalid flash mode\n", __func__);
            wr_val = ci_adv_AeFlashMode_Auto;
        }
        ci_adv_Err ret = ci_adv_AeSetFlashMode(wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetFlashMode(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeFlashMode rd_val;
        ci_adv_Err ret = ci_adv_AeGetFlashMode(&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val) {
        case ci_adv_AeFlashMode_Auto:
            *mode = CAM_AE_FLASH_MODE_AUTO;
            break;
        case ci_adv_AeFlashMode_Off:
            *mode = CAM_AE_FLASH_MODE_OFF;
            break;
        case ci_adv_AeFlashMode_On:
            *mode = CAM_AE_FLASH_MODE_ON;
            break;
        case ci_adv_AeFlashMode_DaySync:
            *mode = CAM_AE_FLASH_MODE_DAY_SYNC;
            break;
        case ci_adv_AeFlashMode_SlowSync:
            *mode = CAM_AE_FLASH_MODE_SLOW_SYNC;
            break;
        default:
            LOGE("%s: get invalid flash mode\n", __func__);
            *mode = CAM_AE_FLASH_MODE_AUTO;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeIsFlashNecessary(bool *used)
{
    if(!mInitied)
        return AAA_FAIL;

    *used = false;
    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeIsFlashNecessary(used);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetFlickerMode(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeFlickerMode wr_val;
        switch (mode)
        {
        case CAM_AE_FLICKER_MODE_OFF:
            wr_val = ci_adv_AeFlickerMode_Off;
            break;
        case CAM_AE_FLICKER_MODE_50HZ:
            wr_val = ci_adv_AeFlickerMode_50Hz;
            break;
        case CAM_AE_FLICKER_MODE_60HZ:
            wr_val = ci_adv_AeFlickerMode_60Hz;
            break;
        case CAM_AE_FLICKER_MODE_AUTO:
            wr_val = ci_adv_AeFlickerMode_Auto;
            break;
        default:
            LOGE("%s: set invalid flicker mode\n", __func__);
            wr_val = ci_adv_AeFlickerMode_Auto;
        }
        ci_adv_Err ret = ci_adv_AeSetFlickerMode(wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetFlickerMode(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeFlickerMode rd_val;
        ci_adv_Err ret = ci_adv_AeGetFlickerMode(&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_AeFlickerMode_Off:
            *mode = CAM_AE_FLICKER_MODE_OFF;
            break;
        case ci_adv_AeFlickerMode_50Hz:
            *mode = CAM_AE_FLICKER_MODE_50HZ;
            break;
        case ci_adv_AeFlickerMode_60Hz:
            *mode = CAM_AE_FLICKER_MODE_60HZ;
            break;
        case ci_adv_AeFlickerMode_Auto:
            *mode = CAM_AE_FLICKER_MODE_AUTO;
            break;
        default:
            LOGE("%s: get invalid flicker mode\n", __func__);
            *mode = CAM_AE_FLICKER_MODE_AUTO;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetManualIso(int sensitivity, bool to_hw)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
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
            ci_adv_Err ret = ci_adv_AeSetManualIso((int)(65536 *fev));
            if(ci_adv_Success != ret)
                return AAA_FAIL;

            LOGD(" *** manual set iso in EV: %f\n", fev);
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetManualIso(int *sensitivity)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        int iev;
        ci_adv_Err ret = ci_adv_AeGetManualIso(&iev);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        *sensitivity = (int)(3.125 * pow(2, ((float) iev / 65536.0)));
        mManualIso = *sensitivity;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetManualAperture(float aperture, bool to_hw)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
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
            ci_adv_Err ret = ci_adv_AeSetManualAperture((int)(65536 * fev));
            if(ci_adv_Success != ret)
                return AAA_FAIL;

            LOGD(" *** manual set aperture in EV: %f\n", fev);
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetManualAperture(float *aperture)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        int iev;
        ci_adv_Err ret = ci_adv_AeGetManualAperture(&iev);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        *aperture = pow(2, (float)iev / (2.0 * 65536.0));
        mManualAperture = *aperture;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetManualShutter(float exp_time, bool to_hw)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
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
            ci_adv_Err ret = ci_adv_AeSetManualShutter((int)(65536 * fev));
            if(ci_adv_Success != ret)
                return AAA_FAIL;

            LOGD(" *** manual set shutter in EV: %f\n", fev);
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetManualShutter(float *exp_time)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        int iev;
        ci_adv_Err ret = ci_adv_AeGetManualShutter(&iev);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        *exp_time = pow(2, -1.0 * ((float)iev / 65536));
        mManualShutter = *exp_time;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfSetManualFocus(int focus, bool to_hw)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        mFocusPosition = focus;

        if (to_hw)
        {

        //fixme: use distance as manual focus control
        //int ret = ci_adv_AfManualFocusAbs(focus);
        int ret = cam_driver_set_focus_posi (main_fd, focus);

            if(0 != ret)
                return AAA_FAIL;
        }

        LOGD(" *** manual set focus distance in cm: %d\n", focus);
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;

}

int AAAProcess::AfGetManualFocus(int *focus)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        *focus = mFocusPosition;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;

}

int AAAProcess::AeSetWindow(const cam_Window *window)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeSetWindow((ci_adv_Window *)window);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetWindow(cam_Window *window)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeGetWindow((ci_adv_Window *)window);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbSetMode (int wb_mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_Success;
        switch (wb_mode) {
        case CAM_AWB_MODE_DAYLIGHT:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_ClearSky);
            break;
        case CAM_AWB_MODE_CLOUDY:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_Cloudiness);
            break;
        case CAM_AWB_MODE_SUNSET:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_FilamentLamp);
            break;
        case CAM_AWB_MODE_TUNGSTEN:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_FilamentLamp);
            break;
        case CAM_AWB_MODE_FLUORESCENT:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_Fluorlamp_N);
            break;
        case CAM_AWB_MODE_WARM_FLUORESCENT:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_Fluorlamp_W);
            break;
        case CAM_AWB_MODE_WARM_INCANDESCENT:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_FilamentLamp);
            break;
        case CAM_AWB_MODE_SHADOW:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            ret = ci_adv_AwbSetLightSource (ci_adv_AwbLightSource_ShadowArea);
            break;
        case CAM_AWB_MODE_MANUAL_INPUT:
            ci_adv_AwbSetMode (ci_adv_AwbMode_Manual);
            break;
        case CAM_AWB_MODE_AUTO:
            ret = ci_adv_AwbSetMode (ci_adv_AwbMode_Auto);
            break;
        default:
            LOGE("%s: set invalid AWB mode\n", __func__);
            ret = ci_adv_AwbSetMode (ci_adv_AwbMode_Auto);
        }
        if (ret != ci_adv_Success)
            return AAA_FAIL;
        mAwbMode = wb_mode;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbGetMode(int *wb_mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        *wb_mode = mAwbMode;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbSetManualColorTemperature(int ct, bool to_hw)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        mColorTemperature = ct;

        if (to_hw)
        {
            ci_adv_Err ret = ci_adv_AwbSetManualColorTemperature(ct);
            if(ci_adv_Success != ret)
                return AAA_FAIL;
        }
        LOGD(" *** manual set color temperture in Kelvin: %d\n", ct);
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbGetManualColorTemperature(int *ct)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        *ct = mColorTemperature;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetBacklightCorrection(bool en)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeBacklightCorrectionMode wr_val;
        if (en == true)
        {
            wr_val = ci_adv_AeBacklightCorrectionMode_On;
        }
        else
        {
            wr_val = ci_adv_AeBacklightCorrectionMode_Off;
        }
        ci_adv_Err ret = ci_adv_AeSetBacklightCorrection (wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetBacklightCorrection(bool *en)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AeBacklightCorrectionMode rd_val;
        ci_adv_Err ret = ci_adv_AeGetBacklightCorrection(&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_AeBacklightCorrectionMode_Off:
            *en = false;
            break;
        case ci_adv_AeBacklightCorrectionMode_On:
            *en = true;
            break;
        default:
            LOGE("%s: get invalid AE backlight correction \n", __func__);
            *en = false;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::SetRedEyeRemoval(bool en)
{
    if(!mInitied)
       return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        mRedEyeRemovalEnabled = en;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::GetRedEyeRemoval(bool *en)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        *en = mRedEyeRemovalEnabled;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbSetMapping(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AwbMap wr_val;
        switch (mode)
        {
        case CAM_AWB_MAP_INDOOR:
            wr_val = ci_adv_AwbMap_Indoor;
            break;
        case CAM_AWB_MAP_OUTDOOR:
            wr_val = ci_adv_AwbMap_Outdoor;
            break;
        default:
            LOGE("%s: set invalid AWB map mode\n", __func__);
            wr_val = ci_adv_AwbMap_Indoor;
        }
        ci_adv_Err ret = ci_adv_AwbSetMap (wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AwbGetMapping(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AwbMap rd_val;
        ci_adv_Err ret = ci_adv_AwbGetMap (&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_AwbMap_Indoor:
            *mode = CAM_AWB_MAP_INDOOR;
            break;
        case ci_adv_AwbMap_Outdoor:
            *mode = CAM_AWB_MAP_OUTDOOR;
            break;
        default:
            LOGE("%s: get invalid AWB map mode\n", __func__);
            *mode = CAM_AWB_MAP_INDOOR;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;

}

int AAAProcess::AfSetMode(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_Success;

        switch (mode) {
        case CAM_AF_MODE_AUTO:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
            ci_adv_AfSetRange (ci_adv_AfRange_Norm);
            break;
        case CAM_AF_MODE_MACRO:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
            ci_adv_AfSetRange (ci_adv_AfRange_Macro);
            break;
        case CAM_AF_MODE_INFINITY:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
            ci_adv_AfSetRange (ci_adv_AfRange_Full);
            break;
        case CAM_AF_MODE_MANUAL:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Manual);
            break;
        default:
            LOGE("%s: set invalid AF mode\n", __func__);
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
            ci_adv_AfSetRange (ci_adv_AfRange_Norm);
            break;
        }
        if (ret != ci_adv_Success)
            return AAA_FAIL;
        mAfMode = mode;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfGetMode(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        *mode = mAfMode;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfSetMeteringMode(int mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AfMeteringMode wr_val;
        switch (mode)
        {
        case CAM_AF_METERING_MODE_AUTO:
            wr_val = ci_adv_AfMeteringMode_Auto;
            break;
        case CAM_AF_METERING_MODE_SPOT:
            wr_val = ci_adv_AfMeteringMode_Spot;
            break;
        default:
            LOGE("%s: set invalid AF meter mode\n", __func__);
            wr_val = ci_adv_AfMeteringMode_Auto;
        }
        ci_adv_Err ret = ci_adv_AfSetMeteringMode(wr_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfGetMeteringMode(int *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AfMeteringMode rd_val;
        ci_adv_Err ret = ci_adv_AfGetMeteringMode(&rd_val);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_val)
        {
        case ci_adv_AfMeteringMode_Auto:
            *mode = CAM_AF_METERING_MODE_AUTO;
            break;
        case ci_adv_AfMeteringMode_Spot:
            *mode = CAM_AF_METERING_MODE_SPOT;
            break;
        default:
            LOGE("%s: get invalid AF meter mode\n", __func__);
            *mode = CAM_AF_METERING_MODE_AUTO;
        }
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfSetWindow(const cam_Window *window)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AfSetWindow((ci_adv_Window *)window);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::AfGetWindow(cam_Window *window)
{
    if(!mInitied)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AfGetWindow((ci_adv_Window *)window);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {
    }

    return AAA_SUCCESS;
}

int AAAProcess::FlushManualSettings(void)
{
    int ret;

    // manual aperture
    if (mAeMode == CAM_AE_MODE_MANUAL || mAeMode == CAM_AE_MODE_APERTURE_PRIORITY)
    {
        ret = AeSetManualAperture (mManualAperture, true);
        if (ret != AAA_SUCCESS)
        {
            LOGE(" error in flush manual aperture\n");
            return AAA_FAIL;
        }
    }

    // manual shutter
    if (mAeMode == CAM_AE_MODE_MANUAL || mAeMode == CAM_AE_MODE_SHUTTER_PRIORITY)
    {
        ret = AeSetManualShutter (mManualShutter, true);
        if (ret != AAA_SUCCESS)
        {
            LOGE(" error in flush manual shutter\n");
            return AAA_FAIL;
        }
    }

    // manual iso
    if (mAeMode == CAM_AE_MODE_MANUAL)
    {
        ret = AeSetManualIso (mManualIso, true);
        if (ret != AAA_SUCCESS)
        {
            LOGE(" error in flush manual iso\n");
            return AAA_FAIL;
        }
    }

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

    // manual color temperature
    if (mAwbMode == CAM_AWB_MODE_MANUAL_INPUT)
    {
        ret = AwbSetManualColorTemperature(mColorTemperature, true);
        if (ret != AAA_SUCCESS)
        {
            LOGE(" error in flush manual color temperature\n");
            return AAA_FAIL;
        }
    }

    return AAA_SUCCESS;
}

/* private interface */
void AAAProcess::Init(void)
{
    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_init();
        mInitied = 1;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

void AAAProcess::Uninit(void)
{
    if(!mInitied)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_uninit();
        mInitied = 0;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }
}

}; // namespace android
