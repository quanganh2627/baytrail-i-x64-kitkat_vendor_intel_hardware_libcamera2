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
#define LOG_TAG "Camera_AAA"

#include "LogHelper.h"
#include "AtomAAA.h"
#include "AtomCommon.h"
#include <math.h>
#include <time.h>

namespace android {

AtomAAA* AtomAAA::mInstance = NULL;

AtomAAA::AtomAAA() :
    mIspFd(-1)
    ,mHas3A(false)
    ,mSensorType(SENSOR_TYPE_NONE)
    ,mAfMode(CAM_AF_MODE_NOT_SET)
    ,mFlashMode(CAM_AE_FLASH_MODE_NOT_SET)
    ,mAwbMode(CAM_AWB_MODE_NOT_SET)
    ,mFocusPosition(0)
    ,mStillAfStart(0)
{
    LOG1("@%s", __FUNCTION__);
    mIspSettings.GBCE_strength = DEFAULT_GBCE_STRENGTH;
    mIspSettings.GBCE_enabled = DEFAULT_GBCE;
    mIspSettings.inv_gamma = false;
}

AtomAAA::~AtomAAA()
{
    LOG1("@%s", __FUNCTION__);
    mInstance = NULL;
}

status_t AtomAAA::init(const char *sensor_id, int fd, const char *otpInjectFile)
{
    Mutex::Autolock lock(m3aLock);
    int init_result;
    init_result = ci_adv_init(sensor_id, fd, otpInjectFile);
    if (init_result == 0) {
        mSensorType = SENSOR_TYPE_RAW;
        mHas3A = true;
    } else {
        mSensorType = SENSOR_TYPE_SOC;
    }
    LOG1("@%s: sensor_id = \"%s\", has3a %d, initRes %d, fd = %d, otpInj %s",
         __FUNCTION__, sensor_id, mHas3A, init_result, fd, otpInjectFile);
    mIspFd = fd;
    return NO_ERROR;
}

status_t AtomAAA::unInit()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_uninit();
    mSensorType = SENSOR_TYPE_NONE;
    mIspFd = -1;
    mHas3A = false;
    mAfMode = CAM_AF_MODE_NOT_SET;
    mAwbMode = CAM_AWB_MODE_NOT_SET;
    mFlashMode = CAM_AE_FLASH_MODE_NOT_SET;
    mFocusPosition = 0;
    return NO_ERROR;
}

status_t AtomAAA::applyIspSettings()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_set_gbce_strength(mIspSettings.GBCE_strength);
    if (ci_adv_set_gamma_effect(mIspSettings.inv_gamma) != 0) {
        mHas3A = false;
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomAAA::switchModeAndRate(AtomMode mode, float fps)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_isp_mode isp_mode;
    switch (mode) {
    case MODE_PREVIEW:
        isp_mode = ia_3a_isp_mode_preview;
        break;
    case MODE_CAPTURE:
        isp_mode = ia_3a_isp_mode_capture;
        break;
    case MODE_VIDEO:
        isp_mode = ia_3a_isp_mode_video;
        break;
    default:
        isp_mode = ia_3a_isp_mode_preview;
        LOGW("SwitchMode: Wrong sensor mode %d", mode);
        break;
    }
    ci_adv_configure(isp_mode, fps);
    return NO_ERROR;
}

status_t AtomAAA::setAeWindow(const CameraWindow *window)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: window = %p (%d,%d,%d,%d,%d)", __FUNCTION__,
            window,
            window->x_left,
            window->y_top,
            window->x_right,
            window->y_bottom,
            window->weight);
    if(!mHas3A)
        return INVALID_OPERATION;
    if(ci_adv_ae_set_window((ia_3a_window *)window) != ci_adv_success)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

status_t AtomAAA::setAfWindow(const CameraWindow *window)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: window = %p (%d,%d,%d,%d,%d)", __FUNCTION__,
            window,
            window->x_left,
            window->y_top,
            window->x_right,
            window->y_bottom,
            window->weight);
    if(!mHas3A)
        return INVALID_OPERATION;
    if(ci_adv_af_set_windows(1,(ia_3a_window *)window) != ci_adv_success)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

status_t AtomAAA::setAfEnabled(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_af_enable(en);
    return NO_ERROR;
}

status_t AtomAAA::setAeSceneMode(SceneMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_ae_exposure_program wr_val;
    switch (mode) {
    case CAM_AE_SCENE_MODE_AUTO:
        wr_val = ia_3a_ae_exposure_program_auto;
        break;
    case CAM_AE_SCENE_MODE_PORTRAIT:
        wr_val = ia_3a_ae_exposure_program_portrait;
        break;
    case CAM_AE_SCENE_MODE_SPORTS:
        wr_val = ia_3a_ae_exposure_program_sports;
        break;
    case CAM_AE_SCENE_MODE_LANDSCAPE:
        wr_val = ia_3a_ae_exposure_program_landscape;
        break;
    case CAM_AE_SCENE_MODE_NIGHT:
        wr_val = ia_3a_ae_exposure_program_night;
        break;
    case CAM_AE_SCENE_MODE_NIGHT_PORTRAIT:
        wr_val = ia_3a_ae_exposure_program_night;
        break;
    case CAM_AE_SCENE_MODE_FIREWORKS:
        wr_val = ia_3a_ae_exposure_program_fireworks;
        break;
    case CAM_AE_SCENE_MODE_TEXT:
        /* This work-around was decided based on : BZ ID: 11915
         * As the text mode support is not yet supported in
         * 3A library, Auto scene mode will be used for the
         * time being.
         */

        //TODO BZ ID: 13566 should fix this issue properly
        //wr_val = ia_3a_ae_exposure_program_text;
        wr_val = ia_3a_ae_exposure_program_auto;
        break;
    default:
        LOGE("Set: invalid AE scene mode: %d. Using AUTO!", mode);
        wr_val = ia_3a_ae_exposure_program_auto;
    }
    ci_adv_err ret = ci_adv_ae_set_exposure_program (wr_val);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

SceneMode AtomAAA::getAeSceneMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    SceneMode mode = CAM_AE_SCENE_MODE_NOT_SET;
    if(!mHas3A)
        return mode;

    ia_3a_ae_exposure_program rd_val;
    if(ci_adv_ae_get_exposure_program (&rd_val) != ci_adv_success)
        return mode;
    switch (rd_val) {
    case ia_3a_ae_exposure_program_auto:
        mode = CAM_AE_SCENE_MODE_AUTO;
        break;
    case ia_3a_ae_exposure_program_portrait:
        mode = CAM_AE_SCENE_MODE_PORTRAIT;
        break;
    case ia_3a_ae_exposure_program_sports:
        mode = CAM_AE_SCENE_MODE_SPORTS;
        break;
    case ia_3a_ae_exposure_program_landscape:
        mode = CAM_AE_SCENE_MODE_LANDSCAPE;
        break;
    case ia_3a_ae_exposure_program_night:
        mode = CAM_AE_SCENE_MODE_NIGHT;
        break;
    case ia_3a_ae_exposure_program_fireworks:
        mode = CAM_AE_SCENE_MODE_FIREWORKS;
        break;
    default:
        LOGE("Get: invalid AE scene mode: %d. Using AUTO!", rd_val);
        mode = CAM_AE_SCENE_MODE_AUTO;
    }

    return mode;
}

status_t AtomAAA::setAeMode(AeMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_ae_mode wr_val;
    switch (mode) {
    case CAM_AE_MODE_AUTO:
        wr_val = ia_3a_ae_mode_auto;
        break;
    case CAM_AE_MODE_MANUAL:
        wr_val = ia_3a_ae_mode_manual;
        break;
    case CAM_AE_MODE_SHUTTER_PRIORITY:
        wr_val = ia_3a_ae_mode_shutter_priority;
        break;
    case CAM_AE_MODE_APERTURE_PRIORITY:
        wr_val = ia_3a_ae_mode_aperture_priority;
        break;
    default:
        LOGE("Set: invalid AE mode: %d. Using AUTO!", mode);
        wr_val = ia_3a_ae_mode_auto;
    }
    ci_adv_err ret = ci_adv_ae_set_mode(wr_val);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t AtomAAA::setAeFlickerMode(FlickerMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;
    ia_3a_ae_flicker_mode theMode;

    switch(mode) {
    case CAM_AE_FLICKER_MODE_50HZ:
        theMode = ia_3a_ae_flicker_mode_50hz;
        break;
    case CAM_AE_FLICKER_MODE_60HZ:
        theMode = ia_3a_ae_flicker_mode_60hz;
        break;
    case CAM_AE_FLICKER_MODE_AUTO:
        theMode = ia_3a_ae_flicker_mode_auto;
        break;
    case CAM_AE_FLICKER_MODE_OFF:
    default:
        theMode = ia_3a_ae_flicker_mode_off;
        break;
    }

    ci_adv_err ret = ci_adv_ae_set_flicker_mode(theMode);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

AeMode AtomAAA::getAeMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    AeMode mode = CAM_AE_MODE_NOT_SET;
    if(!mHas3A)
        return mode;

    ia_3a_ae_mode rd_val;
    if(ci_adv_ae_get_mode(&rd_val) != ci_adv_success)
        return mode;
    switch (rd_val) {
    case ia_3a_ae_mode_auto:
        mode = CAM_AE_MODE_AUTO;
        break;
    case ia_3a_ae_mode_manual:
        mode = CAM_AE_MODE_MANUAL;
        break;
    case ia_3a_ae_mode_shutter_priority:
        mode = CAM_AE_MODE_SHUTTER_PRIORITY;
        break;
    case ia_3a_ae_mode_aperture_priority:
        mode = CAM_AE_MODE_APERTURE_PRIORITY;
        break;
    default:
        LOGE("Get: invalid AE mode: %d. Using AUTO!", rd_val);
        mode = CAM_AE_MODE_AUTO;
    }

    return mode;
}

status_t AtomAAA::setAfMode(AfMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_err ret = ci_adv_success;

    switch (mode) {
    case CAM_AF_MODE_AUTO:
        ret = ci_adv_af_set_mode (ia_3a_af_mode_auto);
        ci_adv_af_set_range (ia_3a_af_range_norm);
        ci_adv_af_set_metering_mode (ia_3a_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_TOUCH:
        ret = ci_adv_af_set_mode (ia_3a_af_mode_auto);
        ci_adv_af_set_range (ia_3a_af_range_full);
        ci_adv_af_set_metering_mode (ia_3a_af_metering_mode_spot);
        break;
    case CAM_AF_MODE_MACRO:
        ret = ci_adv_af_set_mode (ia_3a_af_mode_auto);
        ci_adv_af_set_range (ia_3a_af_range_macro);
        ci_adv_af_set_metering_mode (ia_3a_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_INFINITY:
        ret = ci_adv_af_set_mode (ia_3a_af_mode_manual);
        ci_adv_af_set_range (ia_3a_af_range_full);
        break;
    case CAM_AF_MODE_MANUAL:
        ret = ci_adv_af_set_mode (ia_3a_af_mode_manual);
        ci_adv_af_set_range (ia_3a_af_range_full);
        break;
    default:
        LOGE("Set: invalid AF mode: %d. Using AUTO!", mode);
        mode = CAM_AF_MODE_AUTO;
        ret = ci_adv_af_set_mode (ia_3a_af_mode_auto);
        ci_adv_af_set_range (ia_3a_af_range_norm);
        ci_adv_af_set_metering_mode (ia_3a_af_metering_mode_auto);
        break;
    }
    if (ret != ci_adv_success)
        return UNKNOWN_ERROR;

    mAfMode = mode;

    return NO_ERROR;
}

AfMode AtomAAA::getAfMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return CAM_AF_MODE_NOT_SET;

    return mAfMode;
}

status_t AtomAAA::setAeFlashMode(FlashMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_ae_flash_mode wr_val;
    switch (mode) {
    case CAM_AE_FLASH_MODE_AUTO:
        wr_val = ia_3a_ae_flash_mode_auto;
        break;
    case CAM_AE_FLASH_MODE_OFF:
        wr_val = ia_3a_ae_flash_mode_off;
        break;
    case CAM_AE_FLASH_MODE_ON:
        wr_val = ia_3a_ae_flash_mode_on;
        break;
    case CAM_AE_FLASH_MODE_DAY_SYNC:
        wr_val = ia_3a_ae_flash_mode_day_sync;
        break;
    case CAM_AE_FLASH_MODE_SLOW_SYNC:
        wr_val = ia_3a_ae_flash_mode_slow_sync;
        break;
    case CAM_AE_FLASH_MODE_TORCH:
        wr_val = ia_3a_ae_flash_mode_off;
        break;
    default:
        LOGE("Set: invalid flash mode: %d. Using AUTO!", mode);
        mode = CAM_AE_FLASH_MODE_AUTO;
        wr_val = ia_3a_ae_flash_mode_auto;
    }
    if(ci_adv_ae_set_flash_mode(wr_val) != ci_adv_success)
        return UNKNOWN_ERROR;

    mFlashMode = mode;

    return NO_ERROR;
}

FlashMode AtomAAA::getAeFlashMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return CAM_AE_FLASH_MODE_NOT_SET;

    return mFlashMode;
}

bool AtomAAA::getAeFlashNecessary()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return false;

    bool en;
    if(ci_adv_ae_is_flash_necessary(&en) != ci_adv_success)
        return false;

    LOG1("%s returning %d", __FUNCTION__, en);
    return en;
}

status_t AtomAAA::setAwbMode (AwbMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_err ret = ci_adv_success;
    switch (mode) {
    case CAM_AWB_MODE_DAYLIGHT:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_clear_sky);
        break;
    case CAM_AWB_MODE_CLOUDY:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_cloudiness);
        break;
    case CAM_AWB_MODE_SUNSET:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_filament_lamp);
        break;
    case CAM_AWB_MODE_TUNGSTEN:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_filament_lamp);
        break;
    case CAM_AWB_MODE_FLUORESCENT:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_fluorlamp_n);
        break;
    case CAM_AWB_MODE_WARM_FLUORESCENT:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_fluorlamp_w);
        break;
    case CAM_AWB_MODE_WARM_INCANDESCENT:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_filament_lamp);
        break;
    case CAM_AWB_MODE_SHADOW:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        ret = ci_adv_awb_set_light_source (ia_3a_awb_light_source_shadow_area);
        break;
    case CAM_AWB_MODE_MANUAL_INPUT:
        ci_adv_awb_set_mode (ia_3a_awb_mode_manual);
        break;
    case CAM_AWB_MODE_AUTO:
        ret = ci_adv_awb_set_mode (ia_3a_awb_mode_auto);
        break;
    default:
        LOGE("Set: invalid AWB mode: %d. Using AUTO!", mode);
        mode = CAM_AWB_MODE_AUTO;
        ret = ci_adv_awb_set_mode (ia_3a_awb_mode_auto);
    }
    if (ret != ci_adv_success)
        return UNKNOWN_ERROR;

    mAwbMode = mode;

    return NO_ERROR;
}

AwbMode AtomAAA::getAwbMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return CAM_AWB_MODE_NOT_SET;

    return mAwbMode;
}

status_t AtomAAA::setAeMeteringMode(MeteringMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_ae_metering_mode wr_val;
    switch (mode) {
    case CAM_AE_METERING_MODE_SPOT:
        wr_val = ia_3a_ae_metering_mode_spot;
        break;
    case CAM_AE_METERING_MODE_CENTER:
        wr_val = ia_3a_ae_metering_mode_center;
        break;
    case CAM_AE_METERING_MODE_CUSTOMIZED:
        wr_val = ia_3a_ae_metering_mode_customized;
        break;
    case CAM_AE_METERING_MODE_AUTO:
        wr_val = ia_3a_ae_metering_mode_auto;
        break;
    default:
        LOGE("Set: invalid AE metering mode: %d. Using AUTO!", mode);
        wr_val = ia_3a_ae_metering_mode_auto;
    }
    ci_adv_err ret = ci_adv_ae_set_metering_mode(wr_val);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

MeteringMode AtomAAA::getAeMeteringMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    MeteringMode mode = CAM_AE_METERING_MODE_NOT_SET;
    if(!mHas3A)
        return mode;

    ia_3a_ae_metering_mode rd_val;
    if(ci_adv_ae_get_metering_mode(&rd_val) != ci_adv_success)
        return mode;
    switch (rd_val) {
    case ia_3a_ae_metering_mode_spot:
        mode = CAM_AE_METERING_MODE_SPOT;
        break;
    case ia_3a_ae_metering_mode_center:
        mode = CAM_AE_METERING_MODE_CENTER;
        break;
    case ia_3a_ae_metering_mode_customized:
        mode = CAM_AE_METERING_MODE_CUSTOMIZED;
        break;
    case ia_3a_ae_metering_mode_auto:
        mode = CAM_AE_METERING_MODE_AUTO;
        break;
    default:
        LOGE("Get: invalid AE metering mode: %d. Using AUTO!", rd_val);
        mode = CAM_AE_METERING_MODE_AUTO;
    }

    return mode;
}

status_t AtomAAA::setAeLock(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_ae_lock(en);
    return NO_ERROR;
}

bool AtomAAA::getAeLock()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    bool ret = false;
    if(mSensorType == SENSOR_TYPE_RAW)
        ci_adv_ae_is_locked(&ret);
    return ret;
}

status_t AtomAAA::setAfLock(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(mSensorType == SENSOR_TYPE_RAW)
        ci_adv_af_lock(en);
    return NO_ERROR;
}

bool AtomAAA::getAfLock()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    bool ret;
    if(mSensorType == SENSOR_TYPE_RAW)
        ci_adv_af_is_locked(&ret);
    return ret;
}

status_t AtomAAA::setAwbLock(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(mSensorType == SENSOR_TYPE_RAW)
        ci_adv_awb_lock(en);
    return NO_ERROR;
}

bool AtomAAA::getAwbLock()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    bool ret;
    if(mSensorType == SENSOR_TYPE_RAW)
        ci_adv_awb_is_locked(&ret);
    return ret;
}

status_t AtomAAA::setAeBacklightCorrection(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_err ret = ci_adv_ae_set_backlight_correction (en);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t AtomAAA::setAwbMapping(ia_3a_awb_map mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_err ret = ci_adv_awb_set_map (mode);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

ia_3a_awb_map AtomAAA::getAwbMapping()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    ia_3a_awb_map ret = ia_3a_awb_map_auto;

    if(mSensorType == SENSOR_TYPE_RAW) {
        ia_3a_awb_map rd_val;
        if(ci_adv_awb_get_map (&rd_val) != ci_adv_success)
            return ret;
    }

    return ret;
}

// How many focus windows are supported
size_t AtomAAA::getAfMaxNumWindows()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    size_t ret = 0;
    if(!mHas3A)
        return 0;
    int numWin = ci_adv_af_maxnum_windows();
    if (numWin > 0)
        ret = numWin;
    return ret;
}

// Set one or more focus windows
status_t AtomAAA::setAfWindows(const CameraWindow *windows, size_t numWindows)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: windows = %p, num = %u", __FUNCTION__, windows, numWindows);
    if(!mHas3A)
        return INVALID_OPERATION;
    if (ci_adv_af_set_windows(numWindows, (ia_3a_window*)windows) != ci_adv_success)
        return UNKNOWN_ERROR;
    return NO_ERROR;
}

status_t AtomAAA::setNegativeEffect(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(mSensorType != SENSOR_TYPE_RAW)
        return INVALID_OPERATION;
    mIspSettings.inv_gamma = en;
    return NO_ERROR;
}

status_t AtomAAA::startStillAf()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_af_start();
    mStillAfStart = systemTime();
    return NO_ERROR;
}

status_t AtomAAA::stopStillAf()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_af_stop();
    mStillAfStart = 0;
    return NO_ERROR;
}

ia_3a_af_status AtomAAA::isStillAfComplete()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return ia_3a_af_status_error;

    if (mStillAfStart == 0) {
        // startStillAf wasn't called? return error
        LOGE("Call startStillAf before calling %s!", __FUNCTION__);
        return ia_3a_af_status_error;
    }
    if (((systemTime() - mStillAfStart) / 1000000) > MAX_TIME_FOR_AF) {
        LOGW("Auto-focus sequence for still capture is taking too long. Cancelling!");
        return ia_3a_af_status_cancelled;
    }

    return ci_adv_af_get_status();
}

status_t AtomAAA::getExposureInfo(SensorParams& sensorParams)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    sensorParams.expTime = 0;
    sensorParams.aperture = 0;
    sensorParams.aecApexTv = 0;
    sensorParams.aecApexSv = 0;
    sensorParams.aecApexAv = 0;
    sensorParams.digitalGain = 0;
    ci_adv_ae_get_exp_cfg(&sensorParams.expTime,
            &sensorParams.aperture,
            &sensorParams.aecApexTv,
            &sensorParams.aecApexSv,
            &sensorParams.aecApexAv,
            &sensorParams.digitalGain);

    return NO_ERROR;
}

status_t AtomAAA::getAeManualBrightness(float *ret)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    float val;
    if (ci_adv_ae_get_manual_brightness(&val) != ci_adv_success)
        return UNKNOWN_ERROR;

    *ret = val;
    return NO_ERROR;
}

// Focus operations
status_t AtomAAA::setManualFocus(int focus, bool applyNow)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: focus=%d, applyNow=%d", __FUNCTION__, focus, applyNow);
    if(!mHas3A)
        return INVALID_OPERATION;

    mFocusPosition = focus;

    if (applyNow && ci_adv_af_manual_focus_abs(focus) != 0)
        return UNKNOWN_ERROR;
    LOG1("Set manual focus distance: %dcm", focus);

    return NO_ERROR;
}

status_t AtomAAA::setManualFocusIncrement(int step)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: step=%d", __FUNCTION__, step);
    if(!mHas3A)
        return INVALID_OPERATION;

    if (ci_adv_set_manual_focus_inc(step))
        return UNKNOWN_ERROR;

    mFocusPosition += step;
    LOG1("Set manual focus increment: %d; current focus distance: %dcm", step, mFocusPosition);

    return NO_ERROR;
}

status_t AtomAAA::updateManualFocus()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    if (ci_adv_update_manual_focus_pos())
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t AtomAAA::getAfLensPosRange(ia_3a_af_lens_range *lens_range)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    if (ci_adv_get_lens_range(lens_range))
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

// Get Next position of Lens from the 3a lib
status_t AtomAAA::getNextFocusPosition(int *pos)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    if(ci_adv_get_focus_next_pos(pos))
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

// Get Current position of Lens from the 3a lib (0 < pos < 255)
status_t AtomAAA::getCurrentFocusPosition(int *pos)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    if(ci_adv_get_focus_current_pos(pos))
        return UNKNOWN_ERROR;

    mFocusPosition = *pos;
    return NO_ERROR;
}

// Exposure operations
status_t AtomAAA::applyEv(float bias)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: bias=%.2f", __FUNCTION__, bias);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_err ret = ci_adv_ae_apply_bias(bias);
    if(ci_adv_success != ret) {
        LOGE("Error applying EV: %.2f; ret=%d", bias, ret);
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

status_t AtomAAA::setEv(float bias)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: bias=%.2f", __FUNCTION__, bias);
    if(!mHas3A)
        return INVALID_OPERATION;

    bias = bias > 2 ? 2 : bias;
    bias = bias < -2 ? -2 : bias;
    ci_adv_err ret = ci_adv_ae_set_bias(bias);
    if(ci_adv_success != ret) {
        LOGE("Error setting EV: %.2f; ret=%d", bias, ret);
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

status_t AtomAAA::getEv(float *ret)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;


    if(ci_adv_ae_get_bias(ret) != ci_adv_success)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t AtomAAA::setManualShutter(float expTime)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if (!mHas3A)
        return INVALID_OPERATION;

    float tv;
    if (expTime <=0) {
        LOGE("invalid shutter setting");
        return INVALID_OPERATION;
    }

    tv = -1.0 * (log10(expTime) / log10(2.0));
    ci_adv_err ret = ci_adv_ae_set_manual_shutter(tv);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    LOGD(" *** manual set shutter in EV: %f\n", tv);
    return NO_ERROR;
}

status_t AtomAAA::getManualShutter(float *expTime)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    float tv;
    ci_adv_err ret = ci_adv_ae_get_manual_shutter(&tv);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    *expTime = pow(2, -1.0 * tv);
    return NO_ERROR;
}

status_t AtomAAA::setManualIso(int sensitivity)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if (!mHas3A)
        return INVALID_OPERATION;

    float sv;
    if(sensitivity <= 0)
    {
        LOGE("invalid ISO value");
        return INVALID_OPERATION;
    }

    sv = log10((float)sensitivity / 3.125) / log10(2.0);
    ci_adv_err ret = ci_adv_ae_set_manual_iso(sv);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    LOGD(" *** manual set iso in EV: %f\n", sv);
    return NO_ERROR;
}

status_t AtomAAA::getManualIso(int *ret)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    float ev;
    if(ci_adv_ae_get_manual_iso(&ev) != ci_adv_success)
        return UNKNOWN_ERROR;

    *ret = (int)(3.125 * pow(2, ev));
    return NO_ERROR;
}

status_t AtomAAA::applyPreFlashProcess(FlashStage stage)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_flash_stage wr_stage;
    switch (stage) {
    case CAM_FLASH_STAGE_NONE:
        wr_stage = ia_3a_flash_stage_none;
        break;
    case CAM_FLASH_STAGE_PRE:
        wr_stage = ia_3a_flash_stage_pre;
        break;
    case CAM_FLASH_STAGE_MAIN:
        wr_stage = ia_3a_flash_stage_main;
        break;
    default:
        LOGE("Unknown flash stage: %d", stage);
        return UNKNOWN_ERROR;
    }
    ci_adv_process_for_flash(wr_stage);

    return NO_ERROR;

}

status_t AtomAAA::applyDvsProcess()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_dvs_process();
    return status;
}

status_t AtomAAA::apply3AProcess(bool read_stats,
    const struct timeval capture_timestamp)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s: read_stats = %d", __FUNCTION__, read_stats);
    status_t status = NO_ERROR;
    if(!mHas3A)
        return INVALID_OPERATION;
    if (ci_adv_process_frame(read_stats, &capture_timestamp) != 0) {
        status = UNKNOWN_ERROR;
    }
    return status;
}

status_t AtomAAA::computeCDF(const CiUserBuffer& inputBuf, size_t bufIndex)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: inputBuf=%p, bufIndex=%u", __FUNCTION__, &inputBuf, bufIndex);
    if(!mHas3A)
        return INVALID_OPERATION;

    if (bufIndex > inputBuf.ciBufNum)
        return BAD_VALUE;

    LOG1("Using input CI postview buff %d @%p: (addr=%p, length=%d, width=%d, height=%d, format=%d)",
            bufIndex,
            &inputBuf.ciPostviewBuf[bufIndex],
            inputBuf.ciPostviewBuf[bufIndex].addr,
            inputBuf.ciPostviewBuf[bufIndex].length,
            inputBuf.ciPostviewBuf[bufIndex].width,
            inputBuf.ciPostviewBuf[bufIndex].height,
            inputBuf.ciPostviewBuf[bufIndex].format);
    ia_cp_compute_cdf(&inputBuf.ciPostviewBuf[bufIndex], &inputBuf.cdf[bufIndex]);
    if (inputBuf.cdf[bufIndex] != NULL) {
        LOG1("CDF obtained: %d", *inputBuf.cdf[bufIndex]);
    } else {
        LOG1("CDF obtained: NULL");
    }
    return NO_ERROR;
}

status_t AtomAAA::composeHDR(const CiUserBuffer& inputBuf, const CiUserBuffer& outputBuf, unsigned vividness, unsigned sharpening)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: inputBuf=%p, outputBuf=%p, vividness=%u, sharpening=%u", __FUNCTION__, &inputBuf, &outputBuf, vividness, sharpening);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_cp_hdr_compose (&outputBuf.ciMainBuf[0], &outputBuf.ciPostviewBuf[0], inputBuf.ciMainBuf, inputBuf.ciBufNum, sharpening, vividness, inputBuf.cdf);

    return NO_ERROR;
}

status_t AtomAAA::setSmartSceneDetection(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_dsd_enable(en);
    return NO_ERROR;
}

bool AtomAAA::getSmartSceneDetection()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    bool ret = false;
    if(mHas3A)
        ret = ci_adv_dsd_is_enabled();
    return ret;
}

status_t AtomAAA::getSmartSceneMode(int *sceneMode, bool *sceneHdr)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_dsd_get_scene((ia_aiq_scene_mode*) sceneMode, sceneHdr);
    return NO_ERROR;
}

} //  namespace android
