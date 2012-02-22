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
#define LOG_TAG "Atom_AAA"

#include "LogHelper.h"
#include "AtomAAA.h"
#include "AtomCommon.h"

namespace android {

AtomAAA* AtomAAA::mInstance = NULL;

AtomAAA::AtomAAA() :
    mIspFd(-1)
    ,mHas3A(false)
    ,mSensorType(SENSOR_TYPE_NONE)
    ,mAfMode(CAM_AF_MODE_NOT_SET)
    ,mFlashMode(CAM_AE_FLASH_MODE_NOT_SET)
    ,mAwbMode(CAM_AWB_MODE_NOT_SET)
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

status_t AtomAAA::init(const char *sensor_id, int fd)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: sensor_id = %s, fd = %d", __FUNCTION__, sensor_id, fd);
    if (ci_adv_init(sensor_id, fd) == 0) {
        mSensorType = SENSOR_TYPE_RAW;
        mHas3A = true;
    } else {
        mSensorType = SENSOR_TYPE_SOC;
    }
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
    return NO_ERROR;
}

status_t AtomAAA::applyIspSettings()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_set_gbce_strength(mIspSettings.GBCE_strength);
    if (ci_adv_set_gamma_effect(mIspSettings.GBCE_enabled, mIspSettings.inv_gamma) != 0) {
        mHas3A = false;
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomAAA::switchMode(AtomMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_isp_mode isp_mode;
    switch (mode) {
    case MODE_PREVIEW:
        isp_mode = ci_adv_isp_mode_preview;
        break;
    case MODE_CAPTURE:
        isp_mode = ci_adv_isp_mode_capture;
        break;
    case MODE_VIDEO:
        isp_mode = ci_adv_isp_mode_video;
        break;
    default:
        isp_mode = ci_adv_isp_mode_preview;
        LOGW("SwitchMode: Wrong sensor mode %d", mode);
        break;
    }
    ci_adv_switch_isp_config(isp_mode);
    ci_adv_switch_mode(isp_mode);
    return NO_ERROR;
}

status_t AtomAAA::setFrameRate(float fps)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: fps = %.2f", __FUNCTION__, fps);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_set_frame_rate(CI_ADV_S15_16_FROM_FLOAT(fps));
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
    if(ci_adv_ae_set_window((ci_adv_window *)window) != ci_adv_success)
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
    if(ci_adv_af_set_window((ci_adv_window *)window) != ci_adv_success)
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
    case CAM_AE_SCENE_MODE_NIGHT_PORTRAIT:
        wr_val = ci_adv_ae_exposure_program_night;
        break;
    case CAM_AE_SCENE_MODE_FIREWORKS:
        wr_val = ci_adv_ae_exposure_program_fireworks;
        break;
    case CAM_AE_SCENE_MODE_TEXT:
        /* This work-around was decided based on : BZ ID: 11915
         * As the text mode support is not yet supported in
         * 3A library, Auto scene mode will be used for the
         * time being.
         */

        //TODO BZ ID: 13566 should fix this issue properly
        //wr_val = ci_adv_ae_exposure_program_text;
        wr_val = ci_adv_ae_exposure_program_auto;
        break;
    default:
        LOGE("Set: invalid AE scene mode: %d. Using AUTO!", mode);
        wr_val = ci_adv_ae_exposure_program_auto;
    }
    ci_adv_err ret = ci_adv_ae_set_exposure_program (wr_val);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t AtomAAA::setAeMode(AeMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_ae_mode wr_val;
    switch (mode) {
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
        LOGE("Set: invalid AE mode: %d. Using AUTO!", mode);
        wr_val = ci_adv_ae_mode_auto;
    }
    ci_adv_err ret = ci_adv_ae_set_mode(wr_val);
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

    ci_adv_ae_mode rd_val;
    if(ci_adv_ae_get_mode(&rd_val) != ci_adv_success)
        return mode;
    switch (rd_val) {
    case ci_adv_ae_mode_auto:
        mode = CAM_AE_MODE_AUTO;
        break;
    case ci_adv_ae_mode_manual:
        mode = CAM_AE_MODE_MANUAL;
        break;
    case ci_adv_ae_mode_shutter_priority:
        mode = CAM_AE_MODE_SHUTTER_PRIORITY;
        break;
    case ci_adv_ae_mode_aperture_priority:
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
    if(mSensorType != SENSOR_TYPE_RAW)
        return INVALID_OPERATION;

    ci_adv_err ret = ci_adv_success;

    switch (mode) {
    case CAM_AF_MODE_AUTO:
        ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
        ci_adv_af_set_range (ci_adv_af_range_norm);
        ci_adv_af_set_metering_mode (ci_adv_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_TOUCH:
        ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
        ci_adv_af_set_range (ci_adv_af_range_full);
        ci_adv_af_set_metering_mode (ci_adv_af_metering_mode_spot);
        break;
    case CAM_AF_MODE_MACRO:
        ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
        ci_adv_af_set_range (ci_adv_af_range_macro);
        ci_adv_af_set_metering_mode (ci_adv_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_INFINITY:
        ret = ci_adv_af_set_mode (ci_adv_af_mode_manual);
        ci_adv_af_set_range (ci_adv_af_range_full);
        break;
    case CAM_AF_MODE_MANUAL:
        ret = ci_adv_af_set_mode (ci_adv_af_mode_manual);
        ci_adv_af_set_range (ci_adv_af_range_full);
        break;
    default:
        LOGE("Set: invalid AF mode: %d. Using AUTO!", mode);
        mode = CAM_AF_MODE_AUTO;
        ret = ci_adv_af_set_mode (ci_adv_af_mode_auto);
        ci_adv_af_set_range (ci_adv_af_range_norm);
        ci_adv_af_set_metering_mode (ci_adv_af_metering_mode_auto);
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
    case CAM_AE_FLASH_MODE_TORCH:
        wr_val = ci_adv_ae_flash_mode_off;
        break;
    default:
        LOGE("Set: invalid flash mode: %d. Using AUTO!", mode);
        mode = CAM_AE_FLASH_MODE_AUTO;
        wr_val = ci_adv_ae_flash_mode_auto;
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
        LOGE("Set: invalid AWB mode: %d. Using AUTO!", mode);
        mode = CAM_AWB_MODE_AUTO;
        ret = ci_adv_awb_set_mode (ci_adv_awb_mode_auto);
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

    ci_adv_ae_metering_mode wr_val;
    switch (mode) {
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
        LOGE("Set: invalid AE metering mode: %d. Using AUTO!", mode);
        wr_val = ci_adv_ae_metering_mode_auto;
    }
    ci_adv_err ret = ci_adv_ae_set_metering_mode(wr_val);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
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

    ci_adv_ae_backlight_correction_mode wr_val;
    if (en) {
        wr_val = ci_adv_ae_backlight_correction_mode_on;
    } else {
        wr_val = ci_adv_ae_backlight_correction_mode_off;
    }
    ci_adv_err ret = ci_adv_ae_set_backlight_correction (wr_val);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t AtomAAA::setRedEyeRemoval(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;
    ci_adv_redeye_enable(en);
    return NO_ERROR;
}

bool AtomAAA::getRedEyeRemoval()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return false;
   return ci_adv_redeye_is_enabled();
}

status_t AtomAAA::setAwbMapping(AwbMapping mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_awb_map wr_val;
    switch (mode) {
    case CAM_AWB_MAP_AUTO:
        wr_val = ci_adv_awb_map_auto;
        break;
    case CAM_AWB_MAP_INDOOR:
        wr_val = ci_adv_awb_map_indoor;
        break;
    case CAM_AWB_MAP_OUTDOOR:
        wr_val = ci_adv_awb_map_outdoor;
        break;
    default:
        LOGE("Set: invalid AWB map mode: %d. Using AUTO!", mode);
        wr_val = ci_adv_awb_map_auto;
    }
    ci_adv_err ret = ci_adv_awb_set_map (wr_val);
    if(ci_adv_success != ret)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

AwbMapping AtomAAA::getAwbMapping()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    AwbMapping ret = CAM_AWB_MAP_AUTO;

    if(mSensorType == SENSOR_TYPE_RAW) {
        ci_adv_awb_map rd_val;
        if(ci_adv_awb_get_map (&rd_val) != ci_adv_success)
            return ret;
        switch (rd_val)
        {
        case ci_adv_awb_map_indoor:
            ret = CAM_AWB_MAP_INDOOR;
            break;
        case ci_adv_awb_map_outdoor:
            ret = CAM_AWB_MAP_OUTDOOR;
            break;
        default:
            LOGE("Get: Invalid AWB map mode");
            ret = CAM_AWB_MAP_INDOOR;
        }
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
    if (ci_adv_af_set_window_multi(numWindows, (ci_adv_window*)windows) != ci_adv_success)
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

ci_adv_af_status AtomAAA::isStillAfComplete()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return ci_adv_af_status_error;

    if (mStillAfStart == 0) {
        // startStillAf wasn't called? return error
        LOGE("Call startStillAf before calling %s!", __FUNCTION__);
        return ci_adv_af_status_error;
    }
    if (((systemTime() - mStillAfStart) / 1000000) > MAX_TIME_FOR_AF) {
        LOGW("Auto-focus sequence for still capture is taking too long. Cancelling!");
        return ci_adv_af_status_canceled;
    }

    return ci_adv_af_is_complete();
}

status_t AtomAAA::applyPreFlashProcess(FlashStage stage)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    ci_adv_flash_stage wr_stage;
    switch (stage) {
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
        LOGE("Unknown flash stage: %d", stage);
        return UNKNOWN_ERROR;
    }
    ci_adv_process_for_flash(wr_stage);

    return NO_ERROR;

}

status_t AtomAAA::applyRedEyeRemoval(const AtomBuffer &snapshotBuffer, int width, int height, int format) {
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: buffer = %p, w = %d, h = %d, f = %d", __FUNCTION__, &snapshotBuffer, width, height, format);
    status_t status = NO_ERROR;
    ci_adv_user_buffer user_buf;

    if(!mHas3A)
        return INVALID_OPERATION;

    switch (format) {
    case V4L2_PIX_FMT_NV12:
        user_buf.format = ci_adv_frame_format_nv12;
        break;
    case V4L2_PIX_FMT_YUV420:
        user_buf.format = ci_adv_frame_format_yuv420;
        break;
    default:
        LOGE("RedEyeRemoval: unsupported frame format: %s", v4l2Fmt2Str(format));
        return INVALID_OPERATION;
    }
    user_buf.addr = snapshotBuffer.buff->data;
    user_buf.width = width;
    user_buf.height = height;
    user_buf.length = snapshotBuffer.buff->size;
    user_buf.format = format;
    ci_adv_correct_redeyes(&user_buf);

    return status;
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

int AtomAAA::apply3AProcess(bool read_stats)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s: read_stats = %d", __FUNCTION__, read_stats);
    status_t status = NO_ERROR;
    if(!mHas3A)
        return INVALID_OPERATION;
    if (ci_adv_process_frame(read_stats) != 0) {
        status = UNKNOWN_ERROR;
    }
    return status;
}
} //  namespace android
