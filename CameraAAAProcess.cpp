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
#include "CameraAAAProcess.h"

namespace android {


AAAProcess::AAAProcess(unsigned int sensortype)
    : mAeEnabled(0),
      mAfEnabled(0),
      mAwbEnabled(0),
      mRedEyeRemovalEnabled(0),
      mStillStabilizationEnabled(0),
      mGdcEnabled(0),
      mAwbMode(0),
      mAfMode(0),
      mSensorType(~0),
      mInitied(0),
      mAfStillFrames(0)

{
    mSensorType = sensortype;
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

    if(!mAwbEnabled)
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

    if(!mAwbEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_awb_apply_results();
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

void AAAProcess::SwitchMode(CI_ISP_MODE mode)
{
    if(!mInitied)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_switch_mode(mode);
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

int AAAProcess::AfStillIsComplete(void)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAfStillEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        int ret = ci_adv_af_is_complete();
        if(!ret)
            return AAA_FAIL;
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

    if(!mAfEnabled)
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

    if(!mAfEnabled)
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

    if(!mAfEnabled)
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

    if(!mAfEnabled)
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

void AAAProcess::DoRedeyeRemoval(struct user_buffer *user_buf)
{
    if(!mInitied)
        return;

    if(!mRedEyeRemovalEnabled)
        return;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_do_redeye_removal(user_buf);
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

int AAAProcess::AeSetMode(ci_adv_AeMode mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeSetMode(mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetMode(ci_adv_AeMode *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeGetMode(mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetMeteringMode(ci_adv_AeMeteringMode mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeSetMeteringMode(mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetMeteringMode(ci_adv_AeMeteringMode *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeGetMeteringMode(mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetEv(int bias)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        bias = bias > 2 ? 2 : bias;
        bias = bias < -2 ? -2 : bias;
        ci_adv_Err ret = ci_adv_AeSetBias(bias * 65536);
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

int AAAProcess::AeGetEv(int *bias)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeGetBias(bias);
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
        ci_adv_AeExposureProgram wr_mode;
        switch (mode) {
        case CAM_SCENE_MODE_AUTO:
            wr_mode = ci_adv_AeExposureProgram_Auto;
            break;
        case CAM_SCENE_MODE_PORTRAIT:
            wr_mode = ci_adv_AeExposureProgram_Portrait;
            break;
        case CAM_SCENE_MODE_SPORTS:
            wr_mode = ci_adv_AeExposureProgram_Sports;
            break;
        case CAM_SCENE_MODE_LANDSCAPE:
            wr_mode = ci_adv_AeExposureProgram_Landscape;
            break;
        case CAM_SCENE_MODE_NIGHT:
            wr_mode = ci_adv_AeExposureProgram_Night;
            break;
        case CAM_SCENE_MODE_FIREWORKS:
            wr_mode = ci_adv_AeExposureProgram_Fireworks;
            break;
        default:
            wr_mode = ci_adv_AeExposureProgram_Auto;
        }
        ci_adv_Err ret = ci_adv_AeSetExposureProgram (wr_mode);
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
        ci_adv_AeExposureProgram rd_mode;
        ci_adv_Err ret = ci_adv_AeGetExposureProgram (&rd_mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_mode) {
        case ci_adv_AeExposureProgram_Auto:
            *mode = CAM_SCENE_MODE_AUTO;
            break;
        case ci_adv_AeExposureProgram_Portrait:
            *mode = CAM_SCENE_MODE_PORTRAIT;
            break;
        case ci_adv_AeExposureProgram_Sports:
            *mode = CAM_SCENE_MODE_SPORTS;
            break;
        case ci_adv_AeExposureProgram_Landscape:
            *mode = CAM_SCENE_MODE_LANDSCAPE;
            break;
        case ci_adv_AeExposureProgram_Night:
            *mode = CAM_SCENE_MODE_NIGHT;
            break;
        case ci_adv_AeExposureProgram_Fireworks:
            *mode = CAM_SCENE_MODE_FIREWORKS;
            break;
        default:
            *mode = CAM_SCENE_MODE_AUTO;
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
        ci_adv_AeFlashMode wr_mode;
        switch (mode) {
        case CAM_FLASH_MODE_AUTO:
            wr_mode = ci_adv_AeFlashMode_Auto;
            break;
        case CAM_FLASH_MODE_OFF:
            wr_mode = ci_adv_AeFlashMode_Off;
            break;
        case CAM_FLASH_MODE_ON:
            wr_mode = ci_adv_AeFlashMode_On;
            break;
        default:
            wr_mode = ci_adv_AeFlashMode_Auto;
        }
        ci_adv_Err ret = ci_adv_AeSetFlashMode(wr_mode);
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
        ci_adv_AeFlashMode rd_mode;
        ci_adv_Err ret = ci_adv_AeGetFlashMode(&rd_mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
        switch (rd_mode) {
        case ci_adv_AeFlashMode_Auto:
            *mode = CAM_FLASH_MODE_AUTO;
            break;
        case ci_adv_AeFlashMode_Off:
            *mode = CAM_FLASH_MODE_OFF;
            break;
        case ci_adv_AeFlashMode_On:
            *mode = CAM_FLASH_MODE_ON;
            break;
        default:
            *mode = CAM_FLASH_MODE_AUTO;
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

int AAAProcess::AeSetFlickerMode(cam_aeflicker_mode_t mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeSetFlickerMode((ci_adv_AeFlickerMode)mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeGetFlickerMode(cam_aeflicker_mode_t *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeGetFlickerMode((ci_adv_AeFlickerMode *)mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AeSetManualIso(int sensitivity)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeSetManualIso(sensitivity);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
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

    if(!mAeEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AeGetManualIso(sensitivity);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
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

    if(!mAeEnabled)
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

    if(!mAeEnabled)
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

    if(!mAwbEnabled)
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
        case CAM_AWB_MODE_AUTO:
        default:
            ret = ci_adv_AwbSetMode (ci_adv_AwbMode_Auto);
            break;
        }

        if (ret != ci_adv_Success)
            return AAA_FAIL;
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

    if(!mAwbEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_AwbLightSource ls;
        ci_adv_Err err;
        ci_adv_AwbMode mode;

        err = ci_adv_AwbGetMode (&mode);
        if (err != ci_adv_Success) {
            return AAA_FAIL;
        }

        if (mode == ci_adv_AwbMode_Auto) {
            *wb_mode = CAM_AWB_MODE_AUTO;
            return AAA_SUCCESS;
        }

        err = ci_adv_AwbGetLightSource (&ls);
        if (err != ci_adv_Success) {
            return AAA_FAIL;
        }

        switch (ls) {
        case ci_adv_AwbLightSource_FilamentLamp:
            *wb_mode = CAM_AWB_MODE_TUNGSTEN;
            break;
        case ci_adv_AwbLightSource_Cloudiness:
            *wb_mode = CAM_AWB_MODE_CLOUDY;
            break;
        case ci_adv_AwbLightSource_ShadowArea:
            *wb_mode = CAM_AWB_MODE_SHADOW;
            break;
        case ci_adv_AwbLightSource_Fluorlamp_W:
            *wb_mode = CAM_AWB_MODE_WARM_FLUORESCENT;
        case ci_adv_AwbLightSource_Fluorlamp_N:
        case ci_adv_AwbLightSource_Fluorlamp_D:
            *wb_mode = CAM_AWB_MODE_FLUORESCENT;
            break;
        case ci_adv_AwbLightSource_ClearSky:
            *wb_mode = CAM_AWB_MODE_DAYLIGHT;
        default:
            *wb_mode = CAM_AWB_MODE_AUTO;
            break;
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

    if(!mAfEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_Success;

        switch (mode) {
        case CAM_FOCUS_MODE_MACRO:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
            ci_adv_AfSetRange (ci_adv_AfRange_Macro);
            break;
        case CAM_FOCUS_MODE_NORM:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
            ci_adv_AfSetRange (ci_adv_AfRange_Norm);
            break;
        case CAM_FOCUS_MODE_FULL:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
            ci_adv_AfSetRange (ci_adv_AfRange_Full);
            break;
        case CAM_FOCUS_MODE_AUTO:
        default:
            ret = ci_adv_AfSetMode (ci_adv_AfMode_Auto);
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

    if(!mAfEnabled)
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

int AAAProcess::AfSetMeteringMode(ci_adv_AfMeteringMode mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAfEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AfSetMeteringMode(mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
    }
    else if(ENUM_SENSOR_TYPE_SOC == mSensorType)
    {

    }

    return AAA_SUCCESS;
}

int AAAProcess::AfGetMeteringMode(ci_adv_AfMeteringMode *mode)
{
    if(!mInitied)
        return AAA_FAIL;

    if(!mAfEnabled)
        return AAA_FAIL;

    if(ENUM_SENSOR_TYPE_RAW == mSensorType)
    {
        ci_adv_Err ret = ci_adv_AfGetMeteringMode(mode);
        if(ci_adv_Success != ret)
            return AAA_FAIL;
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

    if(!mAfEnabled)
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

    if(!mAfEnabled)
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
