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
#include "PlatformData.h"
#include "PerformanceTraces.h"
#include "cameranvm.h"
#include <math.h>
#include <time.h>
#include <dlfcn.h>
#include <ia_3a.h>

namespace android {
static AtomISP *gISP; // See BZ 61293

#if ENABLE_PROFILING
    #define PERFORMANCE_TRACES_AAA_PROFILER_START() \
        do { \
            PerformanceTraces::AAAProfiler::enable(true); \
            PerformanceTraces::AAAProfiler::start(); \
           } while(0)

    #define PERFORMANCE_TRACES_AAA_PROFILER_STOP() \
        do {\
            PerformanceTraces::AAAProfiler::stop(); \
           } while(0)
#else
     #define PERFORMANCE_TRACES_AAA_PROFILER_START()
     #define PERFORMANCE_TRACES_AAA_PROFILER_STOP()
#endif

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

static ia_3a_status cb_focus_drive_to_pos(short position, short absolute_pos)
{
    ia_3a_af_update_timestamp();

    if (absolute_pos)
        gISP->sensorMoveFocusToPosition(position);
    else
        gISP->sensorMoveFocusToBySteps(position);

    return ia_3a_status_okay;
}

static ia_3a_af_lens_status cb_focus_status(void)
{
    ia_3a_af_lens_status stat;
    stat = ia_3a_af_lens_status_stop;
    return stat;
}

static bool cb_focus_ready(void)
{
    int status;
    gISP->sensorGetFocusStatus(&status);
    return status & ATOMISP_FOCUS_STATUS_ACCEPTS_NEW_MOVE;
}

static ia_3a_af_hp_status cb_focus_home_position(void)
{
    int status;

    gISP->sensorGetFocusStatus(&status);
    status &= ATOMISP_FOCUS_STATUS_HOME_POSITION;

    if (status == ATOMISP_FOCUS_HP_IN_PROGRESS)
        return ia_3a_af_hp_status_incomplete;
    else if (status == ATOMISP_FOCUS_HP_FAILED)
        return ia_3a_af_hp_status_error;

    return ia_3a_af_hp_status_complete;
}

static void
get_sensor_frame_params(ia_aiq_isp_frame_params *sensor_frame_params, struct atomisp_sensor_mode_data *sensor_mode_data)
{
    ia_3a_sensor_mode_data *ia_sensor_mode_data = (ia_3a_sensor_mode_data*)sensor_mode_data;

    /*TODO: isp frame structure to be changed */
    sensor_frame_params->sensor_native_height = ia_sensor_mode_data->y_end-ia_sensor_mode_data->y_start; /*cropped height*/
    sensor_frame_params->sensor_native_width = ia_sensor_mode_data->x_end-ia_sensor_mode_data->x_start; /*cropped width*/
    sensor_frame_params->sensor_horizontal_binning_denominator = 1;

    sensor_frame_params->sensor_horizontal_binning_numerator = 1;
    sensor_frame_params->sensor_vertical_binning_numerator = 1;
    sensor_frame_params->sensor_vertical_binning_denominator = 1;
    sensor_frame_params->horizontal_offset = ia_sensor_mode_data->x_start;
    sensor_frame_params->vertical_offset = ia_sensor_mode_data->y_start;
    sensor_frame_params->cropped_image_height = ia_sensor_mode_data->output_height * ia_sensor_mode_data->binning_factor_y;
    sensor_frame_params->cropped_image_width = ia_sensor_mode_data->output_width * ia_sensor_mode_data->binning_factor_x;
}

} // extern "C"

AtomAAA* AtomAAA::mInstance = NULL;

AtomAAA::AtomAAA() :
    mHas3A(false)
    ,mSensorType(SENSOR_TYPE_NONE)
    ,mAfMode(CAM_AF_MODE_NOT_SET)
    ,mFlashMode(CAM_AE_FLASH_MODE_NOT_SET)
    ,mAwbMode(CAM_AWB_MODE_NOT_SET)
    ,mFocusPosition(0)
    ,mStillAfStart(0)
    ,mISP(NULL)
{
    LOG1("@%s", __FUNCTION__);
    mPrintFunctions.vdebug = vdebug;
    mPrintFunctions.verror = verror;
    mPrintFunctions.vinfo  = vinfo;
    mIspSettings.GBCE_strength = DEFAULT_GBCE_STRENGTH;
    mIspSettings.GBCE_enabled = DEFAULT_GBCE;
    mIspSettings.inv_gamma = false;

    gISP = NULL;
    memset(&m3ALibState, 0, sizeof(AAALibState));
}

AtomAAA::~AtomAAA()
{
    LOG1("@%s", __FUNCTION__);
    mInstance = NULL;
}

status_t AtomAAA::init(const SensorParams *sensorParameters, AtomISP *isp, const char *otpInjectFile)
{
    Mutex::Autolock lock(m3aLock);
    int init_result;
    mISP = isp;
    gISP = isp;
    init_result = ciAdvInit(sensorParameters, otpInjectFile);
    if (init_result == 0) {
        mSensorType = SENSOR_TYPE_RAW;
        mHas3A = true;
    } else {
        mSensorType = SENSOR_TYPE_SOC;
    }
    LOG1("@%s: tuning_3a_file = \"%s\", has3a %d, initRes %d, otpInj %s",
         __FUNCTION__, sensorParameters->tuning3aFile, mHas3A, init_result, otpInjectFile);
    return NO_ERROR;
}

status_t AtomAAA::unInit()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;
    ciAdvUninit();
    mISP = NULL;
    gISP = NULL;
    mSensorType = SENSOR_TYPE_NONE;
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
    ia_3a_gbce_set_strength(mIspSettings.GBCE_strength);
    if (setGammaEffect(mIspSettings.inv_gamma) != 0) {
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
    ciAdvConfigure(isp_mode, fps);
    return NO_ERROR;
}

status_t AtomAAA::setAeWindow(const CameraWindow *window)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s: window = %p (%d,%d,%d,%d,%d)", __FUNCTION__,
            window,
            window->x_left,
            window->y_top,
            window->x_right,
            window->y_bottom,
            window->weight);
    if(!mHas3A)
        return INVALID_OPERATION;
    ia_3a_ae_set_window((const ia_3a_window *)window);
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
    ia_3a_af_set_windows(1, (const ia_3a_window *)window);
    return NO_ERROR;
}

status_t AtomAAA::setAfEnabled(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;
    ia_3a_af_enable(en);
    return NO_ERROR;
}

status_t AtomAAA::setAeSceneMode(SceneMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

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
    ia_3a_ae_set_exposure_program(wr_val);

    return NO_ERROR;
}

SceneMode AtomAAA::getAeSceneMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    SceneMode mode = CAM_AE_SCENE_MODE_NOT_SET;

    ia_3a_ae_exposure_program rd_val = ia_3a_ae_get_exposure_program();
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
    ia_3a_ae_set_mode(wr_val);

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

    ia_3a_ae_set_flicker_mode(theMode);
    return NO_ERROR;
}

AeMode AtomAAA::getAeMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    AeMode mode = CAM_AE_MODE_NOT_SET;
    if(!mHas3A)
        return mode;

    ia_3a_ae_mode rd_val = ia_3a_ae_get_mode();
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

    switch (mode) {
    case CAM_AF_MODE_CONTINUOUS:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_auto);
        ia_3a_af_set_focus_range(ia_3a_af_range_norm);
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_AUTO:
        // we use hyperfocal default lens position in hyperfocal mode
        ia_3a_af_set_focus_mode(ia_3a_af_mode_hyperfocal);
        ia_3a_af_set_focus_range(ia_3a_af_range_full);
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_TOUCH:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_auto);
        ia_3a_af_set_focus_range(ia_3a_af_range_full);
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_spot);
        break;
    case CAM_AF_MODE_MACRO:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_auto);
        ia_3a_af_set_focus_range(ia_3a_af_range_macro);
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_INFINITY:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_infinity);
        ia_3a_af_set_focus_range(ia_3a_af_range_full);
        break;
    case CAM_AF_MODE_FIXED:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_hyperfocal);
        ia_3a_af_set_focus_range(ia_3a_af_range_full);
        break;
    case CAM_AF_MODE_MANUAL:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_manual);
        ia_3a_af_set_focus_range(ia_3a_af_range_full);
        break;
    case CAM_AF_MODE_FACE:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_auto);
        ia_3a_af_set_focus_range(ia_3a_af_range_norm);
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_spot);
        break;
    default:
        LOGE("Set: invalid AF mode: %d. Using AUTO!", mode);
        mode = CAM_AF_MODE_AUTO;
        ia_3a_af_set_focus_mode(ia_3a_af_mode_auto);
        ia_3a_af_set_focus_range(ia_3a_af_range_norm);
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_auto);
        break;
    }

    mAfMode = mode;

    return NO_ERROR;
}

AfMode AtomAAA::getAfMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);
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
    ia_3a_ae_set_flash_mode(wr_val);
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

bool AtomAAA::getAfNeedAssistLight()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return false;

    bool en = ia_3a_af_need_assist_light();

    LOG1("%s returning %d", __FUNCTION__, en);
    return en;
}

bool AtomAAA::getAeFlashNecessary()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return false;

    bool en = ia_3a_ae_is_flash_necessary();

    LOG1("%s returning %d", __FUNCTION__, en);
    return en;
}

status_t AtomAAA::setAwbMode (AwbMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    switch (mode) {
    case CAM_AWB_MODE_DAYLIGHT:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_clear_sky);
        break;
    case CAM_AWB_MODE_CLOUDY:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_cloudiness);
        break;
    case CAM_AWB_MODE_SUNSET:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_filament_lamp);
        break;
    case CAM_AWB_MODE_TUNGSTEN:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_filament_lamp);
        break;
    case CAM_AWB_MODE_FLUORESCENT:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_fluorlamp_n);
        break;
    case CAM_AWB_MODE_WARM_FLUORESCENT:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_fluorlamp_w);
        break;
    case CAM_AWB_MODE_WARM_INCANDESCENT:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_filament_lamp);
        break;
    case CAM_AWB_MODE_SHADOW:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        ia_3a_awb_set_light_source (ia_3a_awb_light_source_shadow_area);
        break;
    case CAM_AWB_MODE_MANUAL_INPUT:
        ia_3a_awb_set_mode (ia_3a_awb_mode_manual);
        break;
    case CAM_AWB_MODE_AUTO:
        ia_3a_awb_set_mode (ia_3a_awb_mode_auto);
        break;
    default:
        LOGE("Set: invalid AWB mode: %d. Using AUTO!", mode);
        mode = CAM_AWB_MODE_AUTO;
        ia_3a_awb_set_mode (ia_3a_awb_mode_auto);
    }

    mAwbMode = mode;

    return NO_ERROR;
}

AwbMode AtomAAA::getAwbMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    return mAwbMode;
}

status_t AtomAAA::setAeMeteringMode(MeteringMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

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
    ia_3a_ae_set_metering_mode(wr_val);

    return NO_ERROR;
}

MeteringMode AtomAAA::getAeMeteringMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);
    MeteringMode mode = CAM_AE_METERING_MODE_NOT_SET;

    ia_3a_ae_metering_mode rd_val = ia_3a_ae_get_metering_mode();
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
    ia_3a_ae_lock(en);
    return NO_ERROR;
}

bool AtomAAA::getAeLock()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    bool ret = false;
    if(mSensorType == SENSOR_TYPE_RAW)
        ret = ia_3a_ae_is_locked();
    return ret;
}

status_t AtomAAA::setAfLock(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(mSensorType == SENSOR_TYPE_RAW)
        ia_3a_af_lock(en);
    return NO_ERROR;
}

bool AtomAAA::getAfLock()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    bool ret = false;
    if(mSensorType == SENSOR_TYPE_RAW)
        ret = ia_3a_af_is_locked();
    return ret;
}

ia_3a_af_status AtomAAA::getCAFStatus()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);

    return ia_3a_af_get_still_status();
}

status_t AtomAAA::setAwbLock(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(mSensorType == SENSOR_TYPE_RAW)
        ia_3a_awb_lock(en);
    return NO_ERROR;
}

bool AtomAAA::getAwbLock()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    bool ret = false;
    if(mSensorType == SENSOR_TYPE_RAW)
        ret = ia_3a_awb_is_locked();
    return ret;
}

status_t AtomAAA::setAeBacklightCorrection(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_ae_enable_backlight_correction(en);

    return NO_ERROR;
}

status_t AtomAAA::setTNR(bool en)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

status_t AtomAAA::setAwbMapping(ia_3a_awb_map mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_awb_set_map(mode);

    return NO_ERROR;
}

ia_3a_awb_map AtomAAA::getAwbMapping()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    ia_3a_awb_map ret = ia_3a_awb_map_auto;

    if(mSensorType == SENSOR_TYPE_RAW)
        ret = ia_3a_awb_get_map();
    return ret;
}

// How many metering windows are supported
size_t AtomAAA::getAeMaxNumWindows()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    if(!mHas3A)
        return 0;

    // TODO: add ask from 3A, if there is added support for that

    return 1;
}

// How many focus windows are supported
size_t AtomAAA::getAfMaxNumWindows()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    size_t ret = 0;
    if(!mHas3A)
        return 0;
    int numWin = ia_3a_af_get_max_windows();
    if (numWin > 0)
        ret = numWin;
    return ret;
}

// Set one or more focus windows
status_t AtomAAA::setAfWindows(const CameraWindow *windows, size_t numWindows)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s: windows = %p, num = %u", __FUNCTION__, windows, numWindows);
    if(!mHas3A)
        return INVALID_OPERATION;

    for (size_t i = 0; i < numWindows; ++i) {
        LOG2("@%s: window(%u) = (%d,%d,%d,%d,%d)", __FUNCTION__, i,
             windows[i].x_left,
             windows[i].y_top,
             windows[i].x_right,
             windows[i].y_bottom,
             windows[i].weight);
    }

    ia_3a_af_set_windows(numWindows, (const ia_3a_window*)windows);
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

    ia_3a_af_set_focus_mode(ia_3a_af_mode_auto);
    ia_3a_af_still_start();
    mStillAfStart = systemTime();
    return NO_ERROR;
}

status_t AtomAAA::stopStillAf()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_af_still_stop();
    if (mAfMode == CAM_AF_MODE_AUTO) {
        ia_3a_af_set_focus_mode(ia_3a_af_mode_manual);
    }
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

    return ia_3a_af_get_still_status();
}

status_t AtomAAA::getExposureInfo(SensorAeConfig& aeConfig)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    // evBias not reset, so not using memset
    aeConfig.expTime = 0;
    aeConfig.aperture = 0;
    aeConfig.aecApexTv = 0;
    aeConfig.aecApexSv = 0;
    aeConfig.aecApexAv = 0;
    aeConfig.digitalGain = 0;
    getAeExpCfg(&aeConfig.expTime,
            &aeConfig.aperture,
            &aeConfig.aecApexTv,
            &aeConfig.aecApexSv,
            &aeConfig.aecApexAv,
            &aeConfig.digitalGain);

    return NO_ERROR;
}

status_t AtomAAA::getAeManualBrightness(float *ret)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    *ret = ia_3a_ae_get_manual_brightness();
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

    if (applyNow)
        ia_3a_af_set_manual_focus_position(focus);
    LOG1("Set manual focus distance: %dcm", focus);

    return NO_ERROR;
}

status_t AtomAAA::setManualFocusIncrement(int step)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: step=%d", __FUNCTION__, step);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_af_increase_manual_focus_position(step);

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

    ia_3a_af_update_manual_focus_position();

    return NO_ERROR;
}

status_t AtomAAA::getAfLensPosRange(ia_3a_af_lens_range *lens_range)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_af_get_lens_range(lens_range);

    return NO_ERROR;
}

// Get Next position of Lens from the 3a lib
status_t AtomAAA::getNextFocusPosition(int *pos)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    *pos = ia_3a_af_get_next_focus_position();

    return NO_ERROR;
}

// Get Current position of Lens from the 3a lib (0 < pos < 255)
status_t AtomAAA::getCurrentFocusPosition(int *pos)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    *pos = ia_3a_af_get_current_focus_position();

    mFocusPosition = *pos;
    return NO_ERROR;
}

// Exposure operations
status_t AtomAAA::applyEv(float bias)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: bias=%.2f", __FUNCTION__, bias);
    int ret;
    if(!mHas3A)
        return INVALID_OPERATION;

    ia_3a_ae_apply_bias(bias, &m3ALibState.results);
    ret = applyResults();
    /* we should set everytime for bias */
    if (!m3ALibState.results.exposure_changed)
      mISP->sensorSetExposure(&m3ALibState.results.exposure);

    if (ret != 0)
        return UNKNOWN_ERROR;
    else
        return NO_ERROR;
}

status_t AtomAAA::setEv(float bias)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: bias=%.2f", __FUNCTION__, bias);

    bias = bias > 2 ? 2 : bias;
    bias = bias < -2 ? -2 : bias;
    ia_3a_ae_set_bias(bias);

    return NO_ERROR;
}

status_t AtomAAA::getEv(float *ret)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    *ret = ia_3a_ae_get_bias();

    return NO_ERROR;
}

status_t AtomAAA::setGDC(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);

    if(!mHas3A || enableGdc(en) != 0)
        return INVALID_OPERATION;

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
    ia_3a_ae_set_manual_shutter_speed(tv);

    LOGD(" *** manual set shutter in EV: %f\n", tv);
    return NO_ERROR;
}

status_t AtomAAA::getManualShutter(float *expTime)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;

    float tv = ia_3a_ae_get_manual_shutter_speed();

    *expTime = pow(2, -1.0 * tv);
    return NO_ERROR;
}

status_t AtomAAA::setManualIso(int sensitivity)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    float sv;
    if(sensitivity <= 0)
    {
        LOGE("invalid ISO value");
        return INVALID_OPERATION;
    }

    sv = log10((float)sensitivity / 3.125) / log10(2.0);
    ia_3a_ae_set_manual_iso(sv);

    LOGD(" *** manual set iso in EV: %f\n", sv);
    return NO_ERROR;
}

status_t AtomAAA::getManualIso(int *ret)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    float ev = ia_3a_ae_get_manual_iso();

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
    processForFlash(wr_stage);

    return NO_ERROR;

}

status_t AtomAAA::apply3AProcess(bool read_stats,
    const struct timeval capture_timestamp)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s: read_stats = %d", __FUNCTION__, read_stats);
    status_t status = NO_ERROR;
    if(!mHas3A)
        return INVALID_OPERATION;
    if (ciAdvProcessFrame(read_stats, &capture_timestamp) != 0) {
        status = UNKNOWN_ERROR;
    }
    return status;
}

status_t AtomAAA::setSmartSceneDetection(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    if(!mHas3A)
        return INVALID_OPERATION;
    ia_3a_dsd_enable(en);
    return NO_ERROR;
}

bool AtomAAA::getSmartSceneDetection()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);
    bool ret = false;
    if(mHas3A)
        ret = ia_3a_dsd_is_enabled();
    return ret;
}

status_t AtomAAA::getSmartSceneMode(int *sceneMode, bool *sceneHdr)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);
    if(!mHas3A)
        return INVALID_OPERATION;
    ia_3a_dsd_get_scene((ia_aiq_scene_mode*) sceneMode, sceneHdr);
    return NO_ERROR;
}

status_t AtomAAA::setFaces(const ia_face_state& faceState)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    ia_3a_set_faces(&faceState);

    return NO_ERROR;
}

ia_3a_mknote *AtomAAA::get3aMakerNote(ia_3a_mknote_mode mknMode)
{
    Mutex::Autolock lock(m3aLock);
    return ia_3a_mknote_get(mknMode);
}

void AtomAAA::put3aMakerNote(ia_3a_mknote *mknData)
{
    Mutex::Autolock lock(m3aLock);
    if (mknData != NULL)
    {
        ia_3a_mknote_put(mknData);
    }
}

void AtomAAA::reset3aMakerNote(void)
{
    Mutex::Autolock lock(m3aLock);
    ia_3a_mknote_reset();
}

int AtomAAA::add3aMakerNoteRecord(ia_3a_mknote_field_type mkn_format_id,
                                   ia_3a_mknote_field_name mkn_name_id,
                                   const void *record,
                                   unsigned short record_size)
{
    Mutex::Autolock lock(m3aLock);
    ia_3a_mknote_add(mkn_format_id, mkn_name_id, record, record_size);
    return 0;
}

status_t AtomAAA::getGridWindow(AAAWindowInfo& window)
{
    struct atomisp_grid_info gridInfo;

    // Get the 3A grid info
    m3aLock.lock();
    get3aGridInfo(&gridInfo);
    m3aLock.unlock();

    // This is how the 3A library defines the statistics grid window measurements
    // BQ = bar-quad = 2x2 pixels
    window.width = gridInfo.s3a_width * gridInfo.s3a_bqs_per_grid_cell * 2;
    window.height = gridInfo.s3a_height * gridInfo.s3a_bqs_per_grid_cell * 2;

    return NO_ERROR;
}

int AtomAAA::dumpCurrent3aStatToFile(void)
{
    Mutex::Autolock lock(m3aLock);

     if (SENSOR_TYPE_RAW == mSensorType) {
         AAAStatistics cur_stat;
         get3aStat(&cur_stat);
         if (NULL != pFile3aStatDump)
             fprintf(pFile3aStatDump, "%8.3f, %8.3f, %8.3f, %8.3f, %8d, %8.3f, %8.3f, %8.3f\n",
                 cur_stat.bv,
                 cur_stat.tv,
                 cur_stat.sv,
                 cur_stat.av,
                 cur_stat.focus_pos,
                 cur_stat.wb_gain_r,
                 cur_stat.wb_gain_g,
                 cur_stat.wb_gain_b);
     }

     return NO_ERROR;
}

int AtomAAA::init3aStatDump(const char * str_mode)
{
    Mutex::Autolock lock(m3aLock);

    if (SENSOR_TYPE_RAW == mSensorType) {
        char out_filename[80];
        struct timeval cur_time;
        gettimeofday(&cur_time, 0);
        snprintf(out_filename, sizeof(out_filename), "/data/dynamic_stat_%s_%010d_%03d.log", str_mode,
            (unsigned int)(cur_time.tv_sec),
            (int)(cur_time.tv_usec/1000.0));

        pFile3aStatDump = fopen(out_filename, "w");
        if (NULL == pFile3aStatDump) {
            LOGE("error in open file for 3a statistics dump\n");
            return INVALID_OPERATION;
        }
    }

    return NO_ERROR;
}

int AtomAAA::deinit3aStatDump(void)
{
    Mutex::Autolock lock(m3aLock);

    if (SENSOR_TYPE_RAW == mSensorType) {
        if (NULL != pFile3aStatDump) {
            fclose (pFile3aStatDump);
            pFile3aStatDump = NULL;
        }
    }

    return NO_ERROR;
}

int AtomAAA::setFpnTable(const ia_frame *fpn_table)
{
    LOG1("@%s", __FUNCTION__);
    struct v4l2_framebuffer fb;
    fb.fmt.width        = fpn_table->width;
    fb.fmt.height       = fpn_table->height;
    fb.fmt.pixelformat  = V4L2_PIX_FMT_SBGGR16;
    fb.fmt.bytesperline = fpn_table->stride * 2;
    fb.fmt.sizeimage    = fb.fmt.height * fb.fmt.sizeimage;
    fb.base             = fpn_table->data;

    return mISP->setFpnTable(&fb);
}

int AtomAAA::ciAdvInit(const SensorParams *paramFiles, const char *sensorOtpFile)
{
    LOG1("@%s", __FUNCTION__);
    ia_3a_params param;
    ia_binary_data *aicNvm = NULL;

    m3ALibState.boot_events = ci_adv_init_state;
    if (!paramFiles)
      return -1;

    m3ALibState.boot_events = paramFiles->bootEvent;
    param.param_module = open3aParamFile(paramFiles->tuning3aFile);
    if (!param.param_module)
        return -1;

    if (sensorOtpFile) {
        mISP->getSensorDataFromFile(sensorOtpFile, reinterpret_cast<sensorPrivateData *>(&m3ALibState.sensor_data));
        if (m3ALibState.sensor_data.size > 0  && m3ALibState.sensor_data.data != NULL)
            m3ALibState.boot_events |= ci_adv_file_sensor_data;
    } else {
        mISP->sensorGetSensorData(reinterpret_cast<sensorPrivateData *>(&m3ALibState.sensor_data));
        if (m3ALibState.sensor_data.size > 0  && m3ALibState.sensor_data.data != NULL)
            m3ALibState.boot_events |= ci_adv_cam_sensor_data;
    }

    mISP->sensorGetMotorData(reinterpret_cast<sensorPrivateData *>(&m3ALibState.motor_data));
    if (m3ALibState.motor_data.size > 0 && m3ALibState.motor_data.data != NULL)
        m3ALibState.boot_events |= ci_adv_cam_motor_data;

    param.cb_move_focus_position = cb_focus_drive_to_pos;
    param.cb_get_focus_status    = cb_focus_status;
    param.cb_focus_req_ready     = cb_focus_ready;
    param.cb_get_hp_status       = cb_focus_home_position;
    param.param_calibration      = &m3ALibState.sensor_data;
    param.motor_calibration      = &m3ALibState.motor_data;

    // Intel 3A
    if (cameranvm_create(mISP->mCameraInput->name,
        (ia_binary_data *)&m3ALibState.sensor_data,
        (ia_binary_data *)&m3ALibState.motor_data,
        &aicNvm)) {
        return -1;
    }

    if (ia_3a_init(&param,
        &paramFiles->prmFiles,
        &mPrintFunctions,
        sensorOtpFile != NULL,
        &(paramFiles->cpfData),
        (const ia_3a_private_data *)(aicNvm)) < 0) {
        if (m3ALibState.sh3a_params) {
            dlclose(m3ALibState.sh3a_params);
            m3ALibState.sh3a_params = NULL;
        }
        cameranvm_delete(aicNvm);
        return -1;
    }

    cameranvm_delete(aicNvm);

    m3ALibState.fpn_table_loaded = false;
    m3ALibState.gdc_table_loaded = false;
    m3ALibState.stats_valid = false;
    m3ALibState.stats = NULL;
    m3ALibState.stats_valid = false;
    memset(&m3ALibState.results, 0, sizeof(m3ALibState.results));

    LOGD("Initialized 3A library with sensor tuning file %s\n", paramFiles->tuning3aFile);
    return 0;
}

void AtomAAA::ciAdvUninit(void)
{
    LOG1("@%s", __FUNCTION__);
    if (m3ALibState.sensor_data.data) {
        free(m3ALibState.sensor_data.data);
        m3ALibState.sensor_data.data = NULL;
    }
    ia_3a_free_statistics(m3ALibState.stats);
    if (m3ALibState.sh3a_params) {
        dlclose(m3ALibState.sh3a_params);
        m3ALibState.sh3a_params = NULL;
    }

    ia_3a_uninit();
}

/*! \fn  enableEe
 * \brief enable edge enhancement ISP parameter
 * @param enable - enable/disble EE
 */
int AtomAAA::enableEe(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

/*! \fn  enableNr
 * \brief enable noise reduction ISP parameter
 * @param enable - enable/disble NR
 */
int AtomAAA::enableNr(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

/*! \fn  enableDp
 * \brief enable DP ISP parameter
 * @param enable - enable/disble DP
 */
int AtomAAA::enableDp(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

/*! \fn  enableOb
 * \brief enable OB ISP parameter
 * @param enable - enable/disble OB
 */
int AtomAAA::enableOb(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

int AtomAAA::enableShadingCorrection(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

int AtomAAA::setGammaEffect(bool inv_gamma)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

int AtomAAA::enableGbce(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

void AtomAAA::ciAdvConfigure(ia_3a_isp_mode mode, float frame_rate)
{
    LOG1("@%s", __FUNCTION__);
    if(mode == ia_3a_isp_mode_capture)
        ia_3a_mknote_add_uint(ia_3a_mknote_field_name_boot_events, m3ALibState.boot_events);
    /* usually the grid changes as well when the mode changes. */
    reconfigureGrid();
    ia_aiq_isp_frame_params sensor_frame_params;
    get_sensor_frame_params(&sensor_frame_params, &m3ALibState.sensor_mode_data);
    ia_3a_reconfigure(mode, frame_rate, m3ALibState.stats, &sensor_frame_params, &m3ALibState.results);
    applyResults();
}

int AtomAAA::applyResults(void)
{
    LOG2("@%s", __FUNCTION__);
    int ret = 0;

    PERFORMANCE_TRACES_AAA_PROFILER_START();

    /* Apply ISP settings */
    if (m3ALibState.results.aic_output) {
        struct atomisp_parameters *aic_out_struct = (struct atomisp_parameters *)m3ALibState.results.aic_output;
        ret |= mISP->setAicParameter(aic_out_struct);
        ret |= mISP->applyColorEffect();
    }

    /* Apply Sensor settings */
    if (m3ALibState.results.exposure_changed) {
        ret |= mISP->sensorSetExposure(&m3ALibState.results.exposure);
        m3ALibState.results.exposure_changed = false;
    }

    /* Apply Flash settings */
    if (m3ALibState.results.flash_intensity_changed) {
        ret |= mISP->setFlashIntensity(m3ALibState.results.flash_intensity);
        m3ALibState.results.flash_intensity_changed = false;
    }

    PERFORMANCE_TRACES_AAA_PROFILER_STOP();
    return ret;
}

/* returns false for error, true for success */
bool AtomAAA::reconfigureGrid(void)
{
    LOG1("@%s", __FUNCTION__);
    mISP->sensorGetModeInfo(&m3ALibState.sensor_mode_data);
    if (mISP->getIspParameters(&m3ALibState.results.isp_params) < 0)
        return false;

    /* Reconfigure 3A grid */
    ia_3a_set_grid_info(&m3ALibState.results.isp_params.info, &m3ALibState.sensor_mode_data);
    if (m3ALibState.stats)
        ia_3a_free_statistics(m3ALibState.stats);
    m3ALibState.stats = ia_3a_allocate_statistics();
    m3ALibState.stats_valid  = false;

    return true;
}

int AtomAAA::getStatistics(void)
{
    LOG2("@%s", __FUNCTION__);
    int ret;

    PERFORMANCE_TRACES_AAA_PROFILER_START();
    ret = mISP->getIspStatistics(m3ALibState.stats);
    if (ret == EAGAIN) {
        LOGV("buffer for isp statistics reallocated according resolution changing\n");
        if (reconfigureGrid() == false)
            LOGE("error in calling reconfigureGrid()\n");
        ret = mISP->getIspStatistics(m3ALibState.stats);
    }
    PERFORMANCE_TRACES_AAA_PROFILER_STOP();

    if (ret == 0) {
        m3ALibState.stats_valid = true;
        return 0;
    }

    return -1;
}

void *AtomAAA::open3aParamFile(const char *modulename)
{
    void **ptr;
    const char *symbolname = "SensorParameters";

    if (m3ALibState.sh3a_params) {
        LOGE( "*** ERROR: Tried to call open3aParamFile() twice!\n");
        return NULL;
    }

    m3ALibState.sh3a_params = dlopen(modulename, RTLD_NOW);
    if (m3ALibState.sh3a_params == NULL) {
        LOGE("*** ERROR: dlopen('%s') failed! (%s)\n", modulename, dlerror());
        goto err;
    }

    ptr = (void **)dlsym(m3ALibState.sh3a_params, symbolname);
    if (ptr == NULL) {
        LOGE("*** ERROR: dlsym('%s') failed! (%s)\n", symbolname, dlerror());
        goto err;
    }

     if (*ptr == NULL) {
         LOGE( "*** ERROR: module parameter pointer contents is NULL!\n");
         goto err;
    }

    return *ptr;
err:
    if (m3ALibState.sh3a_params) {
        dlclose(m3ALibState.sh3a_params);
        m3ALibState.sh3a_params = NULL;
    }
        return NULL;
}

int AtomAAA::ciAdvProcessFrame(bool read_stats, const struct timeval *frame_timestamp)
{
    LOG2("@%s", __FUNCTION__);
#ifndef MRFL_VP
    int ret;
    ia_3a_aperture aperture;

    if (read_stats && ia_3a_need_statistics()) {
        ret = getStatistics();
        if (ret < 0)
            return -1;
    } else if (!read_stats) {
        /* TODO: find out why we do this here, this looks very strange. */
        reconfigureGrid();
    }

    mISP->sensorGetFNumber(&aperture.num, &aperture.denum);

    if (m3ALibState.stats_valid) {
        ia_3a_main(frame_timestamp, m3ALibState.stats, &aperture, &m3ALibState.results);
        applyResults();
    }
#else
    (void)(read_stats);
    (void)(frame_timestamp);
#endif
    return 0;
}

int AtomAAA::processForFlash(ia_3a_flash_stage stage)
{
    LOG1("@%s", __FUNCTION__);
    int ret;

    if (ia_3a_need_statistics()) {
        ret = getStatistics();
        if (ret < 0)
            return -1;
    }
    if (m3ALibState.stats_valid) {
        ia_3a_main_for_flash(m3ALibState.stats, stage, &m3ALibState.results);
        applyResults();
    }

    return 0;
}

void AtomAAA::get3aGridInfo(struct atomisp_grid_info *pgrid)
{
    LOG2("@%s", __FUNCTION__);
    *pgrid = m3ALibState.results.isp_params.info;
}

void AtomAAA::get3aStat(AAAStatistics *pstat)
{
    LOG1("@%s", __FUNCTION__);
    ia_3a_awb_gain digital_gain;
    ia_3a_awb_get_digital_gain(&digital_gain);

    pstat->bv        = ia_3a_ae_get_manual_brightness();
    pstat->tv        = ia_3a_ae_get_manual_shutter_speed();
    pstat->av        = ia_3a_ae_get_manual_aperture();
    pstat->sv        = ia_3a_ae_get_manual_iso();
    pstat->focus_pos = ia_3a_af_get_current_focus_position();
    pstat->wb_gain_r = IA_3A_S15_16_TO_FLOAT(digital_gain.r);
    pstat->wb_gain_g = IA_3A_S15_16_TO_FLOAT(digital_gain.g);
    pstat->wb_gain_b = IA_3A_S15_16_TO_FLOAT(digital_gain.b);
}

/*! \fn  getAfScore
 *  \brief Returns focus score, calculated from the window with size,
 *  selected by ci_adv_set_af_score_window().
 *  @param average_enabled - when TRUE, score is an average from window
 *  grid cells, otherwise score is a sum.
 */
int AtomAAA::getAfScore(bool average_enabled)
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    ret = getStatistics();
    if (ret != -1)
       ret = ia_3a_af_get_score(m3ALibState.stats, average_enabled);
    return ret;
}

int AtomAAA::enableFpn(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

int AtomAAA::enableGdc(bool enable)
{
    // No longer supported, use CPF instead
    LOGE("%s: ERROR, should not be in here", __FUNCTION__);
    return NO_ERROR;
}

/*! \fn  getAEExpCfg
 * \brief Get sensor's configuration for AE
 * exp_time: Preview exposure time
 * aperture: Aperture
 * Get the AEC outputs (which we hope are used by the sensor)
 * @param exp_time - exposure time
 * @param aperture - aperture
 * @param aec_apex_Tv - Shutter speed
 * @param aec_apex_Sv - Sensitivity
 * @param aec_apex_Av - Aperture
 * @param digital_gain - digital_gain
 */
void AtomAAA::getAeExpCfg(int *exp_time, int *aperture,
                     int *aec_apex_Tv, int *aec_apex_Sv, int *aec_apex_Av,
                     float *digital_gain)
{
    LOG1("@%s", __FUNCTION__);
    ia_3a_ae_result ae_res;

    mISP->sensorGetExposureTime(exp_time);
    mISP->sensorGetAperture(aperture);
    ia_3a_ae_get_generic_result(&ae_res);

    *digital_gain = IA_3A_S15_16_TO_FLOAT(ae_res.global_digital_gain);
    *aec_apex_Tv = ae_res.tv;
    *aec_apex_Sv = ae_res.sv;
    *aec_apex_Av = ae_res.av;
}

status_t AtomAAA::set3AColorEffect(v4l2_colorfx effect)
{
    LOG1("@%s: effect = %d", __FUNCTION__, effect);
    status_t status = NO_ERROR;

    status = mISP->setColorEffect(effect);
    if (status != NO_ERROR) {
        return UNKNOWN_ERROR;
    }
    return status;
}


} //  namespace android
