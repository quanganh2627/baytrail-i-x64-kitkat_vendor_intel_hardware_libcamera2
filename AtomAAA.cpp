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
#include "gdctool.h"

namespace android {
static IHWSensorControl *gSensorCI; // See BZ 61293
static IHWLensControl   *gLensCI;
static IHWFlashControl  *gFlashCI;

/**
 * When image data injection is used, read OTP data from
 * this file.
 *
 * Note: camera HAL working directory is "/data" (at least upto ICS)
 */
static const char *privateOtpInjectFileName = "otp_data.bin";

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
        gLensCI->moveFocusToPosition(position);
    else
        gLensCI->moveFocusToBySteps(position);

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
    gLensCI->getFocusStatus(&status);
    return status & ATOMISP_FOCUS_STATUS_ACCEPTS_NEW_MOVE;
}

static ia_3a_af_hp_status cb_focus_home_position(void)
{
    int status;

    gLensCI->getFocusStatus(&status);
    status &= ATOMISP_FOCUS_STATUS_HOME_POSITION;

    if (status == ATOMISP_FOCUS_HP_IN_PROGRESS)
        return ia_3a_af_hp_status_incomplete;
    else if (status == ATOMISP_FOCUS_HP_FAILED)
        return ia_3a_af_hp_status_error;

    return ia_3a_af_hp_status_complete;
}

} // extern "C"

AtomAAA::AtomAAA(HWControlGroup &hwcg) :
     mSensorType(SENSOR_TYPE_NONE)
    ,mAfMode(CAM_AF_MODE_NOT_SET)
    ,mPublicAeMode(CAM_AE_MODE_AUTO)
    ,mFlashMode(CAM_AE_FLASH_MODE_NOT_SET)
    ,mAwbMode(CAM_AWB_MODE_NOT_SET)
    ,mFocusPosition(0)
    ,mStillAfStart(0)
    ,mStillAfAssist(false)
    ,mISP(hwcg.mIspCI)
    ,mFlashCI(hwcg.mFlashCI)
    ,mSensorCI(hwcg.mSensorCI)
    ,mTimePreviousFlash(0)
    ,mTimeAssistRequired(0)
{
    LOG1("@%s", __FUNCTION__);
    mPrintFunctions.vdebug = vdebug;
    mPrintFunctions.verror = verror;
    mPrintFunctions.vinfo  = vinfo;

    gSensorCI = hwcg.mSensorCI;
    gLensCI = hwcg.mLensCI;
    gFlashCI = hwcg.mFlashCI;
    memset(&m3ALibState, 0, sizeof(AAALibState));
    mSensorType = PlatformData::sensorType(mISP->getCurrentCameraId());
}

AtomAAA::~AtomAAA()
{
    LOG1("@%s", __FUNCTION__);
}

status_t AtomAAA::init3A()
{
    LOG1("@%s", __FUNCTION__);

    status_t status = _init3A();

    // We don't need this memory anymore
    PlatformData::AiqConfig.clear();

    return status;
}

status_t AtomAAA::_init3A()
{
    Mutex::Autolock lock(m3aLock);
    status_t status;
    int init_result = 0;
    SensorParams sensorParams;
    const char* otp_file = mISP->isFileInjectionEnabled()? privateOtpInjectFileName: NULL;

    status = mSensorCI->getSensorParams(&sensorParams);
    if (status != NO_ERROR) {
        LOGE("Error retrieving sensor params");
        return status;
    }

    init_result = ciAdvInit(&sensorParams, otp_file);

    LOG1("@%s: tuning_3a_file = \"%s\", initRes %d, otpInj %s",
         __FUNCTION__, sensorParams.tuning3aFile, init_result, otp_file);
    return NO_ERROR;
}

status_t AtomAAA::deinit3A()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    ciAdvUninit();
    mISP = NULL;
    gSensorCI = NULL;
    gLensCI = NULL;
    mSensorType = SENSOR_TYPE_NONE;
    mAfMode = CAM_AF_MODE_NOT_SET;
    mAwbMode = CAM_AWB_MODE_NOT_SET;
    mFlashMode = CAM_AE_FLASH_MODE_NOT_SET;
    mFocusPosition = 0;
    return NO_ERROR;
}

status_t AtomAAA::switchModeAndRate(AtomMode mode, float fps)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

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
    case MODE_CONTINUOUS_CAPTURE:
        isp_mode = ia_3a_isp_mode_continuous;
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
    ia_3a_af_set_windows(1, (const ia_3a_window *)window);
    return NO_ERROR;
}

status_t AtomAAA::setAfEnabled(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
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

/** add iso mode setting*/
status_t AtomAAA::setIsoMode(IsoMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    ia_3a_ae_iso_mode wr_val;
    switch (mode) {
    case CAM_AE_ISO_MODE_AUTO:
        wr_val = ia_3a_ae_iso_mode_auto;
        break;
    case CAM_AE_ISO_MODE_MANUAL:
        wr_val = ia_3a_ae_iso_mode_manual;
        break;
    default:
        LOGE("Set: invalid AE mode: %d. Using AUTO!", mode);
        wr_val = ia_3a_ae_iso_mode_auto;
    }
    ia_3a_ae_set_iso_mode(wr_val);
    return NO_ERROR;
}

IsoMode AtomAAA::getIsoMode(void)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    IsoMode mode = CAM_AE_ISO_MODE_NOT_SET;

    ia_3a_ae_iso_mode rd_val = ia_3a_ae_get_iso_mode();
    switch (rd_val) {
    case ia_3a_ae_iso_mode_auto:
        mode = CAM_AE_ISO_MODE_AUTO;
        break;
    case ia_3a_ae_iso_mode_manual:
        mode = CAM_AE_ISO_MODE_MANUAL;
        break;
    default:
        LOGE("Get: invalid AE ISO mode: %d. Using AUTO!", rd_val);
        mode = CAM_AE_ISO_MODE_AUTO;
    }

    return mode;
}

status_t AtomAAA::setAeFlickerMode(FlickerMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
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
    case CAM_AF_MODE_MACRO:
        ia_3a_af_set_focus_mode(ia_3a_af_mode_manual);
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

    return mAfMode;
}

void AtomAAA::setPublicAeMode(AeMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);
    mPublicAeMode = mode;
}

AeMode AtomAAA::getPublicAeMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);

    return mPublicAeMode;
}

status_t AtomAAA::setAeFlashMode(FlashMode mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

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
        if (mFlashMode != CAM_AE_FLASH_MODE_TORCH)
            gFlashCI->setTorch(TORCH_INTENSITY);
        wr_val = ia_3a_ae_flash_mode_off;
        break;
    default:
        LOGE("Set: invalid flash mode: %d. Using AUTO!", mode);
        mode = CAM_AE_FLASH_MODE_AUTO;
        wr_val = ia_3a_ae_flash_mode_auto;
    }
    if (mFlashMode == CAM_AE_FLASH_MODE_TORCH
        && mode != CAM_AE_FLASH_MODE_TORCH)
        gFlashCI->setTorch(0);
    ia_3a_ae_set_flash_mode(wr_val);
    mFlashMode = mode;

    return NO_ERROR;
}

FlashMode AtomAAA::getAeFlashMode()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);

    return mFlashMode;
}

bool AtomAAA::getAfNeedAssistLight()
{
    Mutex::Autolock lock(m3aLock);
    return getAfNeedAssistLight_Locked();
}

bool AtomAAA::getAfNeedAssistLight_Locked()
{
    LOG1("@%s", __FUNCTION__);

    bool en = ia_3a_af_need_assist_light();

    if (en)
        mTimeAssistRequired = systemTime();

    LOG1("%s returning %d", __FUNCTION__, en);
    return en;
}

bool AtomAAA::getAeFlashNecessary()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);

    // due to this AE algorithm having slight issues with previous flash-illuminated
    // frames affecting the decision, prefer sticky decision making from prior flash usage
    bool en = true;
    uint64_t now = systemTime();
    if (now - mTimePreviousFlash > TIME_STICKY_FLASH_USAGE_NS &&
        now - mTimeAssistRequired > TIME_ASSIST_DECIDES_FLASH_USAGE_NS)
        en = ia_3a_ae_is_flash_necessary();

    LOG2("%s returning %d", __FUNCTION__, en);
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

ia_3a_awb_light_source AtomAAA::getLightSource()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    m3ALightSource = ia_3a_awb_get_light_source();
    return m3ALightSource;
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
    ia_3a_ae_lock(en);
    return NO_ERROR;
}

bool AtomAAA::getAeLock()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    return ia_3a_ae_is_locked();
}

status_t AtomAAA::setAfLock(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
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
    if(mSensorType == SENSOR_TYPE_RAW)
        return ia_3a_af_get_still_status();

    return ia_3a_af_status_idle;
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

status_t AtomAAA::setAwbMapping(ia_3a_awb_map mode)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    ia_3a_awb_set_map(mode);

    return NO_ERROR;
}

ia_3a_awb_map AtomAAA::getAwbMapping()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    return ia_3a_awb_get_map();
}

// How many metering windows are supported
size_t AtomAAA::getAeMaxNumWindows()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    // TODO: add ask from 3A, if there is added support for that

    return 1;
}

// How many focus windows are supported
size_t AtomAAA::getAfMaxNumWindows()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);
    size_t ret = 0;
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

    if (numWindows > 0) {
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_spot);
    } else {
        // No windows set, handle as null-window -> set AF metering "auto"
        ia_3a_af_set_metering_mode(ia_3a_af_metering_mode_auto);
    }

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

status_t AtomAAA::startStillAf()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    if (mFlashMode != CAM_AE_FLASH_MODE_TORCH
        && mFlashMode != CAM_AE_FLASH_MODE_OFF) {
        mStillAfAssist = getAfNeedAssistLight_Locked();
        if (mStillAfAssist) {
            LOG1("Using AF assist light with auto-focus");
            gFlashCI->setTorch(TORCH_INTENSITY);
        }
    }
    // AE lock was taken by the client (See. AAAThread::handleMessageAutoFocus)
    // for AF, this lock was removed for added IA AIQ feature. In AtomAAA side
    // we take the lock here to retain the old functionality.
    ia_3a_ae_lock(true);

    // We have to switch AF mode to auto in order for the AF sequence to run.
    ia_3a_af_set_focus_mode(ia_3a_af_mode_auto);
    ia_3a_af_still_start();
    mStillAfStart = systemTime();
    return NO_ERROR;
}

status_t AtomAAA::stopStillAf()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    ia_3a_af_still_stop();
    // AS the IA 3A library seems to forget that it was in manual mode
    // after AF sequence was run once, force the state back to manual in
    // the focus modes utilizing manual mode.
    if (mAfMode == CAM_AF_MODE_AUTO || mAfMode == CAM_AF_MODE_MACRO) {
        ia_3a_af_set_focus_mode(ia_3a_af_mode_manual);
    }

    if (mStillAfAssist) {
        LOG1("Turning off Torch for auto-focus");
        gFlashCI->setTorch(0);
    }

    mStillAfStart = 0;
    return NO_ERROR;
}

ia_3a_af_status AtomAAA::isStillAfComplete()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

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
    LOG2("@%s", __FUNCTION__);

    // evBias not reset, so not using memset
    aeConfig.expTime = 0;
    aeConfig.aperture_num = 0;
    aeConfig.aperture_denum = 1;
    aeConfig.aecApexTv = 0;
    aeConfig.aecApexSv = 0;
    aeConfig.aecApexAv = 0;
    aeConfig.digitalGain = 0;
    getAeExpCfg(&aeConfig.expTime,
            &aeConfig.aperture_num,
            &aeConfig.aperture_denum,
            &aeConfig.aecApexTv,
            &aeConfig.aecApexSv,
            &aeConfig.aecApexAv,
            &aeConfig.digitalGain,
            &aeConfig.totalGain);

    return NO_ERROR;
}

status_t AtomAAA::getAeManualBrightness(float *ret)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    *ret = ia_3a_ae_get_manual_brightness();
    return NO_ERROR;
}

// Focus operations
status_t AtomAAA::setManualFocus(int focus, bool applyNow)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: focus=%d, applyNow=%d", __FUNCTION__, focus, applyNow);

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

    ia_3a_af_increase_manual_focus_position(step);

    mFocusPosition += step;
    LOG1("Set manual focus increment: %d; current focus distance: %dcm", step, mFocusPosition);

    return NO_ERROR;
}

status_t AtomAAA::updateManualFocus()
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    ia_3a_af_update_manual_focus_position();

    return NO_ERROR;
}

status_t AtomAAA::getAfLensPosRange(ia_3a_af_lens_range *lens_range)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    ia_3a_af_get_lens_range(lens_range);

    return NO_ERROR;
}

// Get Next position of Lens from the 3a lib
status_t AtomAAA::getNextFocusPosition(int *pos)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

    *pos = ia_3a_af_get_next_focus_position();

    return NO_ERROR;
}

// Get Current position of Lens from the 3a lib (0 < pos < 255)
status_t AtomAAA::getCurrentFocusPosition(int *pos)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

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

    ia_3a_ae_apply_bias(bias, &m3ALibState.results);
    ret = applyResults();
    /* we should set everytime for bias */
    if (!m3ALibState.results.exposure_changed)
        mSensorCI->setExposure(&m3ALibState.results.exposure);

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

status_t AtomAAA::setManualShutter(float expTime)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

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
    LOG2("@%s", __FUNCTION__);

    float ev = ia_3a_ae_get_manual_iso();

    *ret = (int)(3.125 * pow(2, ev));
    return NO_ERROR;
}

status_t AtomAAA::applyPreFlashProcess(FlashStage stage)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s", __FUNCTION__);

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
        mTimePreviousFlash = systemTime();
        break;
    default:
        LOGE("Unknown flash stage: %d", stage);
        return UNKNOWN_ERROR;
    }
    processForFlash(wr_stage);

    return NO_ERROR;

}

status_t AtomAAA::apply3AProcess(bool read_stats,
    const struct timeval capture_timestamp,
    const struct timeval sof_timestamp)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s: read_stats = %d", __FUNCTION__, read_stats);
    status_t status = NO_ERROR;

    if (ciAdvProcessFrame(read_stats, &capture_timestamp, &sof_timestamp) != 0) {
        status = UNKNOWN_ERROR;
    }
    return status;
}

status_t AtomAAA::setSmartSceneDetection(bool en)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: en = %d", __FUNCTION__, en);
    ia_3a_dsd_enable(en);
    return NO_ERROR;
}

bool AtomAAA::getSmartSceneDetection()
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);

    return ia_3a_dsd_is_enabled();
}

status_t AtomAAA::getSmartSceneMode(int *sceneMode, bool *sceneHdr)
{
    Mutex::Autolock lock(m3aLock);
    LOG2("@%s", __FUNCTION__);

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

    return NO_ERROR;
}

int AtomAAA::deinit3aStatDump(void)
{
    Mutex::Autolock lock(m3aLock);

    if (NULL != pFile3aStatDump) {
        fclose (pFile3aStatDump);
        pFile3aStatDump = NULL;
    }

    return NO_ERROR;
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
        mSensorCI->getSensorData(reinterpret_cast<sensorPrivateData *>(&m3ALibState.sensor_data));
        if (m3ALibState.sensor_data.size > 0  && m3ALibState.sensor_data.data != NULL)
            m3ALibState.boot_events |= ci_adv_cam_sensor_data;
    }

    mSensorCI->getMotorData(reinterpret_cast<sensorPrivateData *>(&m3ALibState.motor_data));
    if (m3ALibState.motor_data.size > 0 && m3ALibState.motor_data.data != NULL)
        m3ALibState.boot_events |= ci_adv_cam_motor_data;

    param.cb_move_focus_position = cb_focus_drive_to_pos;
    param.cb_get_focus_status    = cb_focus_status;
    param.cb_focus_req_ready     = cb_focus_ready;
    param.cb_get_hp_status       = cb_focus_home_position;
    param.param_calibration      = &m3ALibState.sensor_data;
    param.motor_calibration      = &m3ALibState.motor_data;

    int isp_vamem_type = 0;
    PlatformData::HalConfig.getValue(isp_vamem_type, CPF::IspVamemType);

    // Intel 3A
    // in a case of an error in parsing (e.g. incorrect data,
    // mismatch in checksum) the pointer to NVM data is null
    cameranvm_create(mSensorCI->getSensorName(),
        (ia_binary_data *)&m3ALibState.sensor_data,
        (ia_binary_data *)&m3ALibState.motor_data,
        &aicNvm);

    if (ia_3a_init(&param,
        &paramFiles->prmFiles,
        &mPrintFunctions,
        sensorOtpFile != NULL,
        &(paramFiles->cpfData),
        (const ia_3a_private_data *)(aicNvm),
        isp_vamem_type) < 0) {
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
        // as the data is stored into a global cache, we will not free it, just
        // NULL the pointer
        m3ALibState.sensor_data.data = NULL;
    }
    ia_3a_free_statistics(m3ALibState.stats);
    if (m3ALibState.sh3a_params) {
        dlclose(m3ALibState.sh3a_params);
        m3ALibState.sh3a_params = NULL;
    }

    ia_3a_uninit();
}

void AtomAAA::ciAdvConfigure(ia_3a_isp_mode mode, float frame_rate)
{
    LOG1("@%s", __FUNCTION__);
    if(mode == ia_3a_isp_mode_capture)
        ia_3a_mknote_add_uint(ia_3a_mknote_field_name_boot_events, m3ALibState.boot_events);
    /* usually the grid changes as well when the mode changes. */
    reconfigureGrid();
    ia_aiq_frame_params sensor_frame_params;
    memset(&sensor_frame_params, 0, sizeof(sensor_frame_params));
    getSensorFrameParams(&sensor_frame_params, &m3ALibState.sensor_mode_data);

    struct atomisp_morph_table *gdc_table = getGdcTable(m3ALibState.sensor_mode_data.output_width, m3ALibState.sensor_mode_data.output_height);
    if (gdc_table) {
        m3ALibState.gdc_table_loaded = true;
        LOG1("Initialise gdc_table size %d x %d ", gdc_table->width, gdc_table->height);
        mISP->setGdcConfig(gdc_table);
        mISP->setGDC(true);
        freeGdcTable(gdc_table);
    }
    else {
        LOG1("Empty GDC table -> GDC disabled");
        m3ALibState.gdc_table_loaded = false;
        mISP->setGDC(false);
    }

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
    }

    /* Apply Sensor settings */
    if (m3ALibState.results.exposure_changed) {
        if (mSensorCI != NULL) {
            int delay = mSensorCI->setExposure(&m3ALibState.results.exposure);
            if (delay < 0)
                ret |= delay;
        } else {
            LOGE("No interface for exposure control");
        }
        m3ALibState.results.exposure_changed = false;
    }

    /* Apply Flash settings */
    if (m3ALibState.results.flash_intensity_changed) {
        ret |= mFlashCI->setFlashIntensity(m3ALibState.results.flash_intensity);
        m3ALibState.results.flash_intensity_changed = false;
    }

    PERFORMANCE_TRACES_AAA_PROFILER_STOP();
    return ret;
}

status_t AtomAAA::setFlash(int numFrames)
{
    return mFlashCI->setFlash(numFrames);
}

/* returns false for error, true for success */
bool AtomAAA::reconfigureGrid(void)
{
    LOG1("@%s", __FUNCTION__);
    mSensorCI->getModeInfo(&m3ALibState.sensor_mode_data);
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

int AtomAAA::ciAdvProcessFrame(bool read_stats, const struct timeval *frame_timestamp, const struct timeval *sof_timestamp)
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

    mSensorCI->getFNumber(&aperture.num, &aperture.denum);

    if (m3ALibState.stats_valid) {
        ia_3a_main(frame_timestamp, sof_timestamp, m3ALibState.stats, &aperture, &m3ALibState.results);
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
 * @param total_gain - total_gain
 */
void AtomAAA::getAeExpCfg(int *exp_time,
                          short unsigned int *aperture_num,
                          short unsigned int *aperture_denum,
                          int *aec_apex_Tv, int *aec_apex_Sv, int *aec_apex_Av,
                          float *digital_gain, float *total_gain)
{
    LOG2("@%s", __FUNCTION__);
    ia_3a_ae_result ae_res;

    mSensorCI->getExposureTime(exp_time);
    mSensorCI->getFNumber(aperture_num, aperture_denum);
    ia_3a_ae_get_generic_result(&ae_res);

    *digital_gain = IA_3A_S15_16_TO_FLOAT(ae_res.global_digital_gain);
    *aec_apex_Tv = ae_res.tv;
    *aec_apex_Sv = ae_res.sv;
    *aec_apex_Av = ae_res.av;
    *total_gain = ((pow(2.0, ((float)ae_res.sv)/65536.0))/(pow(2.0, -7.0/4.0)))/100;
    LOG2("total_gain: %f", *total_gain);
}

status_t AtomAAA::set3AColorEffect(const char *effect)
{
    Mutex::Autolock lock(m3aLock);
    LOG1("@%s: effect = %s", __FUNCTION__, effect);
    status_t status = NO_ERROR;

    ia_aiq_effect aiqEffect = ia_aiq_effect_none;
    if (strncmp(effect, CameraParameters::EFFECT_MONO, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_black_and_white;
    else if (strncmp(effect, CameraParameters::EFFECT_NEGATIVE, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_negative;
    else if (strncmp(effect, CameraParameters::EFFECT_SEPIA, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_sepia;
    else if (strncmp(effect, IntelCameraParameters::EFFECT_STILL_SKY_BLUE, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_sky_blue;
    else if (strncmp(effect, IntelCameraParameters::EFFECT_STILL_GRASS_GREEN, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_grass_green;
    else if (strncmp(effect, IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_LOW, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_skin_whiten_low;
    else if (strncmp(effect, IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_MEDIUM, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_skin_whiten;
    else if (strncmp(effect, IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_HIGH, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_skin_whiten_high;
    else if (strncmp(effect, IntelCameraParameters::EFFECT_VIVID, strlen(effect)) == 0)
        aiqEffect = ia_aiq_effect_vivid;
    else if (strncmp(effect, CameraParameters::EFFECT_NONE, strlen(effect)) != 0){
        LOGE("Color effect not found.");
        status = -1;
        // Fall back to the effect NONE
    }

    ia_3a_set_color_effect(aiqEffect);

    return status;
}

void AtomAAA::getDefaultParams(CameraParameters *params, CameraParameters *intel_params)
{
    LOG2("@%s", __FUNCTION__);
    if (!params) {
        LOGE("params is null!");
        return;
    }

    int cameraId = mISP->getCurrentCameraId();
    // ae mode
    intel_params->set(IntelCameraParameters::KEY_AE_MODE, "auto");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_AE_MODES, "auto,manual,shutter-priority,aperture-priority");

    // 3a lock: auto-exposure lock
    params->set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK, CameraParameters::FALSE);
    params->set(CameraParameters::KEY_AUTO_EXPOSURE_LOCK_SUPPORTED, CameraParameters::TRUE);
    // 3a lock: auto-whitebalance lock
    params->set(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK, CameraParameters::FALSE);
    params->set(CameraParameters::KEY_AUTO_WHITEBALANCE_LOCK_SUPPORTED, CameraParameters::TRUE);

    // Intel/UMG parameters for 3A locks
    // TODO: only needed until upstream key is available for AF lock
    intel_params->set(IntelCameraParameters::KEY_AF_LOCK_MODE, "unlock");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_AF_LOCK_MODES, "lock,unlock");
    // TODO: add UMG-style AE/AWB locking for Test Camera?

    // manual shutter control (Intel extension)
    intel_params->set(IntelCameraParameters::KEY_SHUTTER, "60");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_SHUTTER, "1s,2,4,8,15,30,60,125,250,500");

    // multipoint focus
    params->set(CameraParameters::KEY_MAX_NUM_FOCUS_AREAS, getAfMaxNumWindows());
    // set empty area
    params->set(CameraParameters::KEY_FOCUS_AREAS, "(0,0,0,0,0)");

    // metering areas
    params->set(CameraParameters::KEY_MAX_NUM_METERING_AREAS, getAeMaxNumWindows());
    // set empty area
    params->set(CameraParameters::KEY_METERING_AREAS, "(0,0,0,0,0)");

    // Capture bracketing
    intel_params->set(IntelCameraParameters::KEY_CAPTURE_BRACKET, "none");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_CAPTURE_BRACKET, "none,exposure,focus");

    intel_params->set(IntelCameraParameters::KEY_HDR_IMAGING, PlatformData::defaultHdr(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_HDR_IMAGING, PlatformData::supportedHdr(cameraId));

    intel_params->set(IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL, "off");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_HDR_SAVE_ORIGINAL, "on,off");

    // AWB mapping mode
    intel_params->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_AWB_MAPPING_MODES, "auto,indoor,outdoor");

}

void AtomAAA::getSensorFrameParams(ia_aiq_frame_params *frame_params, struct atomisp_sensor_mode_data *sensor_mode_data)
{
    frame_params->horizontal_crop_offset = sensor_mode_data->crop_horizontal_start;
    frame_params->vertical_crop_offset = sensor_mode_data->crop_vertical_start;
    frame_params->cropped_image_height = sensor_mode_data->crop_vertical_end - sensor_mode_data->crop_vertical_start;
    frame_params->cropped_image_width = sensor_mode_data->crop_horizontal_end - sensor_mode_data->crop_horizontal_start;
    /* TODO: Get scaling factors from sensor configuration parameters */
    frame_params->horizontal_scaling_denominator = 254;
    frame_params->vertical_scaling_denominator = 254;

    if ((frame_params->cropped_image_width == 0) || (frame_params->cropped_image_height == 0)){
    // the driver gives incorrect values for the frame width or height
        frame_params->horizontal_scaling_numerator = 0;
        frame_params->vertical_scaling_numerator = 0;
        LOGE("Invalid sensor frame parameters. Cropped image width: %d, cropped image height: %d",
              frame_params->cropped_image_width, frame_params->cropped_image_height );
        LOGE("This causes lens shading table not to be used.");
    } else {
        frame_params->horizontal_scaling_numerator =
                sensor_mode_data->output_width * 254 * sensor_mode_data->binning_factor_x/ frame_params->cropped_image_width;
        frame_params->vertical_scaling_numerator =
                sensor_mode_data->output_height * 254 * sensor_mode_data->binning_factor_y / frame_params->cropped_image_height;
    }
}

} //  namespace android
