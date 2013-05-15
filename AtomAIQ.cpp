/*
 * Copyright (c) 2012 Intel Corporation.
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

#define LOG_TAG "Camera_AtomAIQ"


#include <math.h>
#include <time.h>
#include <dlfcn.h>
#include <utils/String8.h>

#include "LogHelper.h"
#include "AtomCommon.h"
#include "PlatformData.h"
#include "PerformanceTraces.h"
#include "cameranvm.h"
#include "ia_cmc_parser.h"
#include "PanoramaThread.h"
#include "FeatureData.h"

#include "AtomAIQ.h"
#include "ia_mkn_encoder.h"
#include "ia_mkn_decoder.h"

#define MAX_EOF_SOF_DIFF 200000
#define DEFAULT_EOF_SOF_DELAY 66000
#define EPSILON 0.00001
#define RETRY_COUNT 5

namespace android {


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

#define MAX_STATISTICS_WIDTH 150
#define MAX_STATISTICS_HEIGHT 150
#define IA_AIQ_MAX_NUM_FACES 1

AtomAIQ* AtomAIQ::mInstance = NULL; // ToDo: remove

AtomAIQ::AtomAIQ(AtomISP *anISP) :
    mISP(anISP)
    ,mAfMode(CAM_AF_MODE_NOT_SET)
    ,mStillAfStart(0)
    ,mFocusPosition(0)
    ,mBracketingStops(0)
    ,mAeSceneMode(CAM_AE_SCENE_MODE_NOT_SET)
    ,mAwbMode(CAM_AWB_MODE_NOT_SET)
    ,mAwbRunCount(0)
    ,mMkn(NULL)
{
    LOG1("@%s", __FUNCTION__);
    memset(&m3aState, 0, sizeof(aaa_state));
}

AtomAIQ::~AtomAIQ()
{
    LOG1("@%s", __FUNCTION__);
    mInstance = NULL;
}

status_t AtomAIQ::init3A()
{
    LOG1("@%s", __FUNCTION__);

    status_t status = NO_ERROR;
    ia_err ret = ia_err_none;

    ia_binary_data cpfData;
    status = getAiqConfig(&cpfData);
    if (status != NO_ERROR) {
        LOGE("Error retrieving sensor params");
        return status;
    }

    ia_binary_data *aicNvm = NULL;
    ia_binary_data sensorData, motorData;
    mISP->sensorGetSensorData((sensorPrivateData *) &sensorData);
    mISP->sensorGetMotorData((sensorPrivateData *)&motorData);
    cameranvm_create(mISP->mCameraInput->name,
                         &sensorData,
                         &motorData,
                         &aicNvm) ;
    mMkn = ia_mkn_init(ia_mkn_cfg_compression);
    if(mMkn == NULL)
        LOGE("Error makernote init");
    ret = ia_mkn_enable(mMkn, true);
    if(ret != ia_err_none)
        LOGE("Error makernote init");

    ia_cmc_t *cmc = ia_cmc_parser_init((ia_binary_data*)&(cpfData));
    m3aState.ia_aiq_handle = ia_aiq_init((ia_binary_data*)&(cpfData),
                                         (ia_binary_data*)aicNvm,
                                         MAX_STATISTICS_WIDTH,
                                         MAX_STATISTICS_HEIGHT,
                                         cmc,
                                         mMkn);

    if ((mISP->getCssMajorVersion() == 1) && (mISP->getCssMinorVersion() == 5)){
        m3aState.ia_isp_handle = ia_isp_1_5_init((ia_binary_data*)&(cpfData),
                                                 MAX_STATISTICS_WIDTH,
                                                 MAX_STATISTICS_HEIGHT,
                                                 cmc,
                                                 mMkn);
    }
    else if ((mISP->getCssMajorVersion() == 2) && (mISP->getCssMinorVersion() == 0)){
        m3aState.ia_isp_handle = ia_isp_2_2_init((ia_binary_data*)&(cpfData),
                                                         MAX_STATISTICS_WIDTH,
                                                         MAX_STATISTICS_HEIGHT,
                                                         cmc,
                                                         mMkn);
    }
    else {
        m3aState.ia_isp_handle = NULL;
        LOGE("Ambiguous CSS version used: %d.%d", mISP->getCssMajorVersion(), mISP->getCssMinorVersion());
    }

    ia_cmc_parser_deinit(cmc);

    if(!m3aState.ia_aiq_handle || !m3aState.ia_isp_handle) {
        cameranvm_delete(aicNvm);
        return UNKNOWN_ERROR;
    }

    m3aState.frame_use = ia_aiq_frame_use_preview;
    m3aState.dsd_enabled = false;

    run3aInit();

    cameranvm_delete(aicNvm);
    m3aState.stats = NULL;
    m3aState.stats_valid = false;
    memset(&m3aState.results, 0, sizeof(m3aState.results));

    return status;
}

status_t AtomAIQ::getAiqConfig(ia_binary_data *cpfData)
{
    status_t status = NO_ERROR;

    if (PlatformData::AiqConfig && cpfData != NULL) {
        cpfData->data = PlatformData::AiqConfig.ptr();
        cpfData->size = PlatformData::AiqConfig.size();
        // We don't need this memory anymore
        PlatformData::AiqConfig.clear();
    } else {
        status = UNKNOWN_ERROR;
    }
    return status;
}

status_t AtomAIQ::deinit3A()
{
    LOG1("@%s", __FUNCTION__);

    free(m3aState.faces);
    freeStatistics(m3aState.stats);
    ia_aiq_deinit(m3aState.ia_aiq_handle);
    if ((mISP->getCssMajorVersion() == 1) && (mISP->getCssMinorVersion() == 5))
        ia_isp_1_5_deinit(m3aState.ia_isp_handle);
    else if ((mISP->getCssMajorVersion() == 2) && (mISP->getCssMinorVersion() == 0))
        ia_isp_2_2_deinit(m3aState.ia_isp_handle);
    ia_mkn_uninit(mMkn);
    mISP = NULL;
    mAfMode = CAM_AF_MODE_NOT_SET;
    mAwbMode = CAM_AWB_MODE_NOT_SET;
    mFocusPosition = 0;
    return NO_ERROR;
}

status_t AtomAIQ::switchModeAndRate(AtomMode mode, float fps)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    ia_aiq_frame_use isp_mode;
    switch (mode) {
    case MODE_PREVIEW:
        isp_mode = ia_aiq_frame_use_preview;
        break;
    case MODE_CAPTURE:
        isp_mode = ia_aiq_frame_use_still;
        break;
    case MODE_VIDEO:
        isp_mode = ia_aiq_frame_use_video;
        break;
    case MODE_CONTINUOUS_CAPTURE:
        isp_mode = ia_aiq_frame_use_continuous;
        break;
    default:
        isp_mode = ia_aiq_frame_use_preview;
        LOGW("SwitchMode: Wrong sensor mode %d", mode);
        break;
    }

    m3aState.frame_use = isp_mode;
    mAfInputParameters.frame_use = m3aState.frame_use;
    mAeInputParameters.frame_use = m3aState.frame_use;
    mAwbInputParameters.frame_use = m3aState.frame_use;

    /* usually the grid changes as well when the mode changes. */
    changeSensorMode();

    /* Invalidate AEC results and re-run AEC to get new results for new mode. */
    mAeState.ae_results = NULL;
    return runAeMain();
}

status_t AtomAIQ::setAeWindow(const CameraWindow *window)
{
    // comments from Miikka: There is exposure coordinate in AE input parameters.
    // Around that coordinate (10% of image width/height) exposure is within certain
    // limits (tunable from CPF).

    LOG1("@%s", __FUNCTION__);
    return INVALID_OPERATION;
}

status_t AtomAIQ::setAfWindow(const CameraWindow *window)
{
    LOG1("@%s: window = %p (%d,%d,%d,%d,%d)", __FUNCTION__,
            window,
            window->x_left,
            window->y_top,
            window->x_right,
            window->y_bottom,
            window->weight);

    mAfInputParameters.focus_rect->left = window[0].x_left;
    mAfInputParameters.focus_rect->top = window[0].y_top;
    mAfInputParameters.focus_rect->width = window[0].x_right - window[0].x_left;
    mAfInputParameters.focus_rect->height = window[0].y_bottom - window[0].y_top;

    //ToDo: Make sure that all coordinates passed to AIQ are in format/range defined in ia_coordinate.h.

    return NO_ERROR;
}

status_t AtomAIQ::setAfWindows(const CameraWindow *windows, size_t numWindows)
{
    LOG2("@%s: windows = %p, num = %u", __FUNCTION__, windows, numWindows);
    return setAfWindow(windows);
}

// TODO: no manual setting for scene mode, map that into AE/AF operation mode
status_t AtomAIQ::setAeSceneMode(SceneMode mode)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    mAeSceneMode = mode;
    resetAFParams();
    resetAECParams();
    resetAWBParams();
    switch (mode) {
    case CAM_AE_SCENE_MODE_AUTO:
        break;
    case CAM_AE_SCENE_MODE_PORTRAIT:
        break;
    case CAM_AE_SCENE_MODE_SPORTS:
        mAeInputParameters.operation_mode = ia_aiq_ae_operation_mode_action;
        break;
    case CAM_AE_SCENE_MODE_LANDSCAPE:
        mAfInputParameters.focus_mode = ia_aiq_af_operation_mode_infinity;
        break;
    case CAM_AE_SCENE_MODE_NIGHT:
        mAfInputParameters.focus_mode = ia_aiq_af_operation_mode_hyperfocal;
        // TODO: if user expect low noise low light mode
        // mAeInputParameters.operation_mode = ia_aiq_ae_operation_mode_long_exposure
        // mAeInputParameters.flash_mode = ia_aiq_flash_mode_off
        break;
    case CAM_AE_SCENE_MODE_FIREWORKS:
        mAfInputParameters.focus_mode = ia_aiq_af_operation_mode_infinity;
        //TODO: Below definition is not ready in ia_aiq.h
        //mAeInputParameters.operation_mode = ia_aiq_ae_operation_mode_fireworks;
        mAwbInputParameters.scene_mode = ia_aiq_awb_operation_mode_manual_cct_range;
        m3aState.cct_range.min_cct = 5500;
        m3aState.cct_range.max_cct = 5500;
        mAwbInputParameters.manual_cct_range = &m3aState.cct_range;
        break;
    default:
        LOGE("Get: invalid AE scene mode!");
    }
    return NO_ERROR;
}

SceneMode AtomAIQ::getAeSceneMode()
{
    LOG1("@%s", __FUNCTION__);
    return mAeSceneMode;
}

status_t AtomAIQ::setAeFlickerMode(FlickerMode mode)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    switch(mode) {
    case CAM_AE_FLICKER_MODE_50HZ:
        mAeInputParameters.flicker_reduction_mode = ia_aiq_ae_flicker_reduction_50hz;
        break;
    case CAM_AE_FLICKER_MODE_60HZ:
        mAeInputParameters.flicker_reduction_mode = ia_aiq_ae_flicker_reduction_60hz;
        break;
    case CAM_AE_FLICKER_MODE_AUTO:
        mAeInputParameters.flicker_reduction_mode = ia_aiq_ae_flicker_reduction_auto;
        break;
    case CAM_AE_FLICKER_MODE_OFF:
    default:
        mAeInputParameters.flicker_reduction_mode = ia_aiq_ae_flicker_reduction_off;
        break;
    }

    return NO_ERROR;
}

// No support for aperture priority, always in shutter priority mode
status_t AtomAIQ::setAeMode(AeMode mode)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    mAeMode = mode;
    switch(mode) {
    case CAM_AE_MODE_MANUAL:
        break;
    case CAM_AE_MODE_AUTO:
    case CAM_AE_MODE_SHUTTER_PRIORITY:
    case CAM_AE_MODE_APERTURE_PRIORITY:
    default:
        mAeInputParameters.manual_analog_gain = -1;
        mAeInputParameters.manual_iso = -1;
        mAeInputParameters.manual_exposure_time_us = -1;
        mAeInputParameters.operation_mode = ia_aiq_ae_operation_mode_automatic;
        break;
    }
    return NO_ERROR;
}

AeMode AtomAIQ::getAeMode()
{
    LOG1("@%s", __FUNCTION__);
    return mAeMode;
}

status_t AtomAIQ::setAfMode(AfMode mode)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    switch (mode) {
    case CAM_AF_MODE_CONTINUOUS:
        setAfFocusMode(ia_aiq_af_operation_mode_auto);
        setAfFocusRange(ia_aiq_af_range_normal);
        setAfMeteringMode(ia_aiq_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_AUTO:
        // we use hyperfocal default lens position in hyperfocal mode
        setAfFocusMode(ia_aiq_af_operation_mode_hyperfocal);
        setAfFocusRange(ia_aiq_af_range_extended);
        setAfMeteringMode(ia_aiq_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_TOUCH:
        setAfFocusMode(ia_aiq_af_operation_mode_auto);
        setAfFocusRange(ia_aiq_af_range_extended);
        setAfMeteringMode(ia_aiq_af_metering_mode_touch);
        break;
    case CAM_AF_MODE_MACRO:
        setAfFocusMode(ia_aiq_af_operation_mode_auto);
        setAfFocusRange(ia_aiq_af_range_macro);
        setAfMeteringMode(ia_aiq_af_metering_mode_auto);
        break;
    case CAM_AF_MODE_INFINITY:
        setAfFocusMode(ia_aiq_af_operation_mode_infinity);
        setAfFocusRange(ia_aiq_af_range_extended);
        break;
    case CAM_AF_MODE_FIXED:
        setAfFocusMode(ia_aiq_af_operation_mode_hyperfocal);
        setAfFocusRange(ia_aiq_af_range_extended);
        break;
    case CAM_AF_MODE_MANUAL:
        setAfFocusMode(ia_aiq_af_operation_mode_manual);
        setAfFocusRange(ia_aiq_af_range_extended);
        break;
    case CAM_AF_MODE_FACE:
        setAfFocusMode(ia_aiq_af_operation_mode_auto);
        setAfFocusRange(ia_aiq_af_range_normal);
        setAfMeteringMode(ia_aiq_af_metering_mode_touch);
        break;
    default:
        LOGE("Set: invalid AF mode: %d. Using AUTO!", mode);
        mode = CAM_AF_MODE_AUTO;
        setAfFocusMode(ia_aiq_af_operation_mode_auto);
        setAfFocusRange(ia_aiq_af_range_normal);
        setAfMeteringMode(ia_aiq_af_metering_mode_auto);
        break;
    }

    mAfMode = mode;

    return NO_ERROR;
}

AfMode AtomAIQ::getAfMode()
{
    LOG1("@%s, afMode: %d", __FUNCTION__, mAfMode);
    return mAfMode;
}

status_t AtomAIQ::setAeFlashMode(FlashMode mode)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    // no support for slow sync and day sync flash mode,
    // just use auto flash mode to replace
    ia_aiq_flash_mode wr_val;
    switch (mode) {
    case CAM_AE_FLASH_MODE_ON:
    case CAM_AE_FLASH_MODE_DAY_SYNC:
    case CAM_AE_FLASH_MODE_SLOW_SYNC:
        wr_val = ia_aiq_flash_mode_on;
        break;
    case CAM_AE_FLASH_MODE_OFF:
    case CAM_AE_FLASH_MODE_TORCH:
        wr_val = ia_aiq_flash_mode_off;
        break;
    case CAM_AE_FLASH_MODE_AUTO:
    default:
        LOGE("Set: invalid flash mode: %d. Using AUTO!", mode);
        mode = CAM_AE_FLASH_MODE_AUTO;
        wr_val = ia_aiq_flash_mode_auto;
    }
    mAeFlashMode = mode;
    mAeInputParameters.flash_mode = wr_val;

    return NO_ERROR;
}

FlashMode AtomAIQ::getAeFlashMode()
{
    LOG1("@%s", __FUNCTION__);
    return mAeFlashMode;
}

bool AtomAIQ::getAfNeedAssistLight()
{
    LOG1("@%s", __FUNCTION__);
    bool ret = false;
    if(mAfState.af_results)
        ret = mAfState.af_results->use_af_assist;
    return ret;
}

// ToDo: check if this function is needed or if the info
// could be used directly from AE results
bool AtomAIQ::getAeFlashNecessary()
{
    LOG1("@%s", __FUNCTION__);
    if(mAeState.ae_results)
        return mAeState.ae_results->flash->status;
    else
        return false;
}

status_t AtomAIQ::setAwbMode (AwbMode mode)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);
    ia_aiq_awb_operation_mode wr_val;
    switch (mode) {
    case CAM_AWB_MODE_DAYLIGHT:
        wr_val = ia_aiq_awb_operation_mode_daylight;
        break;
    case CAM_AWB_MODE_CLOUDY:
        wr_val = ia_aiq_awb_operation_mode_partly_overcast;
        break;
    case CAM_AWB_MODE_SUNSET:
        wr_val = ia_aiq_awb_operation_mode_sunset;
        break;
    case CAM_AWB_MODE_TUNGSTEN:
        wr_val = ia_aiq_awb_operation_mode_incandescent;
        break;
    case CAM_AWB_MODE_FLUORESCENT:
        wr_val = ia_aiq_awb_operation_mode_fluorescent;
        break;
    case CAM_AWB_MODE_WARM_FLUORESCENT:
        wr_val = ia_aiq_awb_operation_mode_fluorescent;
        break;
    case CAM_AWB_MODE_WARM_INCANDESCENT:
        wr_val = ia_aiq_awb_operation_mode_incandescent;
        break;
    case CAM_AWB_MODE_SHADOW:
        wr_val = ia_aiq_awb_operation_mode_fully_overcast;
        break;
    case CAM_AWB_MODE_MANUAL_INPUT:
        wr_val = ia_aiq_awb_operation_mode_manual_white;
        break;
    case CAM_AWB_MODE_AUTO:
        wr_val = ia_aiq_awb_operation_mode_auto;
        break;
    default:
        LOGE("Set: invalid AWB mode: %d. Using AUTO!", mode);
        mode = CAM_AWB_MODE_AUTO;
        wr_val = ia_aiq_awb_operation_mode_auto;
    }

    mAwbMode = mode;
    mAwbInputParameters.scene_mode = wr_val;
    LOG2("@%s: Intel mode = %d", __FUNCTION__, mAwbInputParameters.scene_mode);
    return NO_ERROR;
}

AwbMode AtomAIQ::getAwbMode()
{
    LOG1("@%s", __FUNCTION__);
    return mAwbMode;
}

// TODO: add spot., customized, auto???
status_t AtomAIQ::setAeMeteringMode(MeteringMode mode)
{
    LOG1("@%s: mode = %d", __FUNCTION__, mode);

    ia_aiq_ae_metering_mode wr_val;
    switch (mode) {
    case CAM_AE_METERING_MODE_SPOT:
        wr_val = ia_aiq_ae_metering_mode_center;
        break;
    case CAM_AE_METERING_MODE_CENTER:
    case CAM_AE_METERING_MODE_CUSTOMIZED:
    case CAM_AE_METERING_MODE_AUTO:
        wr_val = ia_aiq_ae_metering_mode_evaluative;
        break;
    default:
        LOGE("Set: invalid AE metering mode: %d. Using AUTO!", mode);
        wr_val = ia_aiq_ae_metering_mode_evaluative;
    }
    mAeInputParameters.metering_mode = wr_val;

    return NO_ERROR;
}

MeteringMode AtomAIQ::getAeMeteringMode()
{
    LOG1("@%s", __FUNCTION__);
    MeteringMode mode = CAM_AE_METERING_MODE_NOT_SET;

    ia_aiq_ae_metering_mode rd_val = mAeInputParameters.metering_mode;
    switch (rd_val) {
    case ia_aiq_ae_metering_mode_evaluative:
        mode = CAM_AE_METERING_MODE_SPOT;
        break;
    case ia_aiq_ae_metering_mode_center:
        mode = CAM_AE_METERING_MODE_CENTER;
        break;
    default:
        LOGE("Get: invalid AE metering mode: %d. Using SPOT!", rd_val);
        mode = CAM_AE_METERING_MODE_SPOT;
    }

    return mode;
}

status_t AtomAIQ::setAeLock(bool en)
{
    LOG1("@%s: en = %d", __FUNCTION__, en);
    mAeState.ae_locked = en;
    return NO_ERROR;
}

bool AtomAIQ::getAeLock()
{
    LOG1("@%s", __FUNCTION__);
    bool ret = mAeState.ae_locked;
    return ret;
}

status_t AtomAIQ::setAfLock(bool en)
{
    LOG1("@%s: en = %d", __FUNCTION__, en);
    mAfState.af_locked = en;
    return NO_ERROR;
}

bool AtomAIQ::getAfLock()
{
    LOG1("@%s, af_locked: %d ", __FUNCTION__, mAfState.af_locked);
    bool ret = false;
    ret = mAfState.af_locked;
    return ret;
}

ia_3a_af_status AtomAIQ::getCAFStatus()
{
    LOG2("@%s", __FUNCTION__);
    ia_3a_af_status status = ia_3a_af_status_busy;
    if (mAfState.af_results != NULL) {
        if (mAfState.af_results->status == ia_aiq_af_status_success && (mAfState.af_results->final_lens_position_reached || mStillAfStart == 0)) {
            status = ia_3a_af_status_success;
        }
        else if (mAfState.af_results->status == ia_aiq_af_status_fail && (mAfState.af_results->final_lens_position_reached || mStillAfStart == 0)) {
            status  = ia_3a_af_status_error;
        }
        else {
            status = ia_3a_af_status_busy;
        }
    }
    LOG2("af_results->status:%d", status);
    return status;
}

status_t AtomAIQ::setAwbLock(bool en)
{
    LOG1("@%s: en = %d", __FUNCTION__, en);
    mAwbLocked = en;
    return NO_ERROR;
}

bool AtomAIQ::getAwbLock()
{
    LOG1("@%s, AsbLocked: %d", __FUNCTION__, mAwbLocked);
    bool ret = mAwbLocked;
    return ret;
}

status_t AtomAIQ::set3AColorEffect(const char *effect)
{
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
    if ((mISP->getCssMajorVersion() == 1) && (mISP->getCssMinorVersion()==5))
        mISP15InputParameters.effects = aiqEffect;
    else if ((mISP->getCssMajorVersion() == 2) && (mISP->getCssMinorVersion()==0))
        mISP22InputParameters.effects = aiqEffect;

    return status;
}

void AtomAIQ::setPublicAeMode(AeMode mode)
{
    LOG2("@%s, AeMode: %d", __FUNCTION__, mode);
    mAeMode = mode;
}

AeMode AtomAIQ::getPublicAeMode()
{
    LOG2("@%s, AeMode: %d", __FUNCTION__, mAeMode);
    return mAeMode;
}

void AtomAIQ::setPublicAfMode(AfMode mode)
{
    LOG2("@%s, AfMode: %d", __FUNCTION__, mode);
    mAfMode = mode;
}

AfMode AtomAIQ::getPublicAfMode()
{
    LOG2("@%s, AfMode: %d", __FUNCTION__, mAfMode);
    return mAfMode;
}

status_t AtomAIQ::startStillAf()
{
    LOG1("@%s", __FUNCTION__);
    setAfFocusMode(ia_aiq_af_operation_mode_auto);
    mAfInputParameters.frame_use = ia_aiq_frame_use_still;
    mStillAfStart = systemTime();

    return NO_ERROR;
}

status_t AtomAIQ::stopStillAf()
{
    LOG1("@%s", __FUNCTION__);
    if (mAfMode == CAM_AF_MODE_AUTO) {
        setAfFocusMode(ia_aiq_af_operation_mode_manual);
    }
    mAfInputParameters.frame_use = m3aState.frame_use;

    mStillAfStart = 0;
    return NO_ERROR;
}

ia_3a_af_status AtomAIQ::isStillAfComplete()
{
    LOG2("@%s", __FUNCTION__);
    if (mStillAfStart == 0) {
        // startStillAf wasn't called? return error
        LOGE("Call startStillAf before calling %s!", __FUNCTION__);
        return ia_3a_af_status_error;
    }

    if (((systemTime() - mStillAfStart) / 1000000) > AIQ_MAX_TIME_FOR_AF) {
        LOGW("Auto-focus sequence for still capture is taking too long. Cancelling!");
        return ia_3a_af_status_cancelled;
    }

    ia_3a_af_status ret = getCAFStatus();
    return ret;
}

status_t AtomAIQ::getExposureInfo(SensorAeConfig& aeConfig)
{
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
            &aeConfig.digitalGain);

    return NO_ERROR;
}

// TODO: it needed by exif, so need AIQ provide
status_t AtomAIQ::getAeManualBrightness(float *ret)
{
    LOG1("@%s", __FUNCTION__);
    return INVALID_OPERATION;
}

// Focus operations
status_t AtomAIQ::initAfBracketing(int stops, AFBracketingMode mode)
{
    LOG1("@%s", __FUNCTION__);
    mBracketingStops = stops;
    ia_aiq_af_bracketing_parameters param;
    switch (mode) {
    case CAM_AF_BRACKETING_MODE_SYMMETRIC:
        param.af_bracketing_mode = ia_aiq_af_bracketing_mode_symmetric;
        break;
    case CAM_AF_BRACKETING_MODE_TOWARDS_NEAR:
        param.af_bracketing_mode = ia_aiq_af_bracketing_mode_towards_near;
        break;
    case CAM_AF_BRACKETING_MODE_TOWARDS_FAR:
        param.af_bracketing_mode = ia_aiq_af_bracketing_mode_towards_far;
        break;
    default:
        param.af_bracketing_mode = ia_aiq_af_bracketing_mode_symmetric;
    }
    param.focus_positions = (char) stops;
    //first run AF to get the af result
    runAfMain();
    memcpy(&param.af_results, mAfState.af_results, sizeof(ia_aiq_af_results));
    ia_aiq_af_bracketing_calculate(m3aState.ia_aiq_handle, &param, &mAfBracketingResult);
    for(int i = 0; i < stops; i++)
        LOG1("i=%d, postion=%ld", i, mAfBracketingResult->lens_positions_bracketing[i]);

    return  NO_ERROR;
}

status_t AtomAIQ::setManualFocusIncrement(int steps)
{
    LOG1("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;
    if(steps >= 0 && steps < mBracketingStops) {
        int position = mAfBracketingResult->lens_positions_bracketing[steps];
        int focus_moved = mISP->sensorMoveFocusToPosition(position);
        if(focus_moved != 0)
            ret = UNKNOWN_ERROR;
    }
    return ret;
}

// Exposure operations
// For exposure bracketing
status_t AtomAIQ::applyEv(float bias)
{
    LOG1("@%s: bias=%.2f", __FUNCTION__, bias);

    int ret = setEv(bias);
    if (ret == NO_ERROR)
        ret = runAeMain();

    return ret;
}

status_t AtomAIQ::setEv(float bias)
{
    LOG1("@%s: bias=%.2f", __FUNCTION__, bias);
    if(bias > 4 || bias < -4)
        return BAD_VALUE;
    mAeInputParameters.ev_shift = bias;

    return NO_ERROR;
}

status_t AtomAIQ::getEv(float *ret)
{
    LOG1("@%s", __FUNCTION__);
    *ret = mAeInputParameters.ev_shift;
    return NO_ERROR;
}

// TODO: need confirm if it's correct.
status_t AtomAIQ::setManualShutter(float expTime)
{
    LOG1("@%s, expTime: %f", __FUNCTION__, expTime);
    mAeInputParameters.manual_exposure_time_us = expTime * 1000000;
    return NO_ERROR;
}

status_t AtomAIQ::setManualIso(int sensitivity)
{
    LOG1("@%s - %d", __FUNCTION__, sensitivity);
    mAeInputParameters.manual_iso = sensitivity;
    return NO_ERROR;
}

status_t AtomAIQ::getManualIso(int *ret)
{
    LOG2("@%s - %d", __FUNCTION__, mAeInputParameters.manual_iso);
    *ret = mAeInputParameters.manual_iso;
    return NO_ERROR;
}

status_t AtomAIQ::applyPreFlashProcess(FlashStage stage)
{
    LOG2("@%s", __FUNCTION__);

    status_t ret = NO_ERROR;

    /* AEC needs some timestamp to detect if frame is the same. */
    struct timeval dummy_time;
    dummy_time.tv_sec = stage;
    dummy_time.tv_usec = 0;

    if (stage == CAM_FLASH_STAGE_PRE || stage == CAM_FLASH_STAGE_MAIN)
    {
        /* Store previous state of 3A locks. */
        bool prev_af_lock = getAfLock();
        bool prev_ae_lock = getAeLock();
        bool prev_awb_lock = getAwbLock();

        /* AF is not run during flash sequence. */
        setAfLock(true);

        /* During flash sequence AE and AWB must be enabled in order to calculate correct parameters for the final image. */
        setAeLock(false);
        setAwbLock(false);

        mAeInputParameters.frame_use = ia_aiq_frame_use_still;

        ret =  apply3AProcess(true, dummy_time, dummy_time);

        mAeInputParameters.frame_use = m3aState.frame_use;

        /* Restore previous state of 3A locks. */
        setAfLock(prev_af_lock);
        setAeLock(prev_ae_lock);
        setAwbLock(prev_awb_lock);
    }
    else
    {
        ret =  apply3AProcess(true, dummy_time, dummy_time);
    }
    return ret;
}


status_t AtomAIQ::setFlash(int numFrames)
{
    LOG1("@%s: numFrames = %d", __FUNCTION__, numFrames);
    return mISP->setFlash(numFrames);
}

status_t AtomAIQ::apply3AProcess(bool read_stats,
    const struct timeval capture_timestamp, struct timeval sof_timestamp)
{
    LOG2("@%s: read_stats = %d", __FUNCTION__, read_stats);
    status_t status = NO_ERROR;

    if (read_stats) {
        status = getStatistics(&capture_timestamp, &sof_timestamp);
    }

    if (m3aState.stats_valid) {
        status |= run3aMain();
    }

    return status;
}

status_t AtomAIQ::setSmartSceneDetection(bool en)
{
    LOG1("@%s: en = %d", __FUNCTION__, en);

    m3aState.dsd_enabled = en;
    return NO_ERROR;
}

bool AtomAIQ::getSmartSceneDetection()
{
    LOG2("@%s", __FUNCTION__);
    return m3aState.dsd_enabled;
}

status_t AtomAIQ::getSmartSceneMode(int *sceneMode, bool *sceneHdr)
{
    LOG1("@%s", __FUNCTION__);
    if(sceneMode != NULL && sceneHdr != NULL) {
        *sceneMode = mDetectedSceneMode & ~ia_aiq_scene_mode_hdr;
        *sceneHdr = mDetectedSceneMode & ia_aiq_scene_mode_hdr;
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

status_t AtomAIQ::setFaces(const ia_face_state& faceState)
{
    LOG1("@%s", __FUNCTION__);

    m3aState.faces->num_faces = faceState.num_faces;
    if(m3aState.faces->num_faces > IA_AIQ_MAX_NUM_FACES)
        m3aState.faces->num_faces = IA_AIQ_MAX_NUM_FACES;

    /*ia_aiq assumes that the faces are ordered in the order of importance*/
    memcpy(m3aState.faces->faces, faceState.faces, faceState.num_faces*sizeof(ia_face));

    return NO_ERROR;
}

/* TODO: Replace ia_3a_mknote with ia_binary_data in this API. */
ia_3a_mknote *AtomAIQ::get3aMakerNote(ia_3a_mknote_mode mknMode)
{
    LOG2("@%s", __FUNCTION__);
    ia_mkn_trg mknTarget = ia_mkn_trg_exif;

    ia_3a_mknote *me;
    me = (ia_3a_mknote *)malloc(sizeof(ia_3a_mknote));
    if (!me)
        return NULL;
    if(mknMode == ia_3a_mknote_mode_raw)
        mknTarget = ia_mkn_trg_raw;
    ia_binary_data mkn_binary_data = ia_mkn_prepare(mMkn, mknTarget);

    me->bytes = mkn_binary_data.size;
    me->data = (char*)malloc(me->bytes);
    if (me->data)
    {
        memcpy(me->data, mkn_binary_data.data, me->bytes);
    } else {
        return NULL;
    }
    return me;
}

void AtomAIQ::put3aMakerNote(ia_3a_mknote *mknData)
{
    LOG2("@%s", __FUNCTION__);

    if (mknData) {
        if (mknData->data) {
            free(mknData->data);
            mknData->data = NULL;
        }
        free(mknData);
    }
}

void AtomAIQ::reset3aMakerNote(void)
{
    LOG2("@%s", __FUNCTION__);
    ia_mkn_reset(mMkn);
}

int AtomAIQ::add3aMakerNoteRecord(ia_3a_mknote_field_type mkn_format_id,
                                   ia_3a_mknote_field_name mkn_name_id,
                                   const void *record,
                                   unsigned short record_size)
{
    LOG2("@%s", __FUNCTION__);
    //ToDo: HAL could have its own instance of IA MKN.
    // Before writing makernote into EXIF, the HAL and AIQ makernotes
    // can be merged (there is a function in IA MKN for doing that).
    return INVALID_OPERATION;
}

void AtomAIQ::get3aGridInfo(struct atomisp_grid_info *pgrid)
{
    LOG2("@%s", __FUNCTION__);
    *pgrid = m3aState.results.isp_params.info;
}


status_t AtomAIQ::getGridWindow(AAAWindowInfo& window)
{
    struct atomisp_grid_info gridInfo;

    // Get the 3A grid info
    get3aGridInfo(&gridInfo);

    // This is how the 3A library defines the statistics grid window measurements
    // BQ = bar-quad = 2x2 pixels
    window.width = gridInfo.s3a_width * gridInfo.s3a_bqs_per_grid_cell * 2;
    window.height = gridInfo.s3a_height * gridInfo.s3a_bqs_per_grid_cell * 2;

    return NO_ERROR;
}

int AtomAIQ::setFpnTable(const ia_frame *fpn_table)
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

struct atomisp_3a_statistics * AtomAIQ::allocateStatistics(int grid_size)
{
    LOG2("@%s", __FUNCTION__);
    struct atomisp_3a_statistics *stats;
    stats = (atomisp_3a_statistics*)malloc(sizeof(*stats));
    if (!stats)
        return NULL;

    stats->data = (atomisp_3a_output*)malloc(grid_size * sizeof(*stats->data));
    if (!stats->data) {
        free(stats);
        return NULL;
    }
    LOG2("@%s success", __FUNCTION__);
    return stats;
}

void AtomAIQ::freeStatistics(struct atomisp_3a_statistics *stats)
{
    if (stats) {
        if (stats->data)
            free(stats->data);
        free(stats);
    }
}


int AtomAIQ::run3aInit()
{
    LOG1("@%s", __FUNCTION__);
    memset(&m3aState.curr_grid_info, 0, sizeof(m3aState.curr_grid_info));
    memset(&m3aState.rgbs_grid, 0, sizeof(m3aState.rgbs_grid));
    memset(&m3aState.af_grid, 0, sizeof(m3aState.af_grid));

    m3aState.faces = (ia_face_state*)malloc(sizeof(ia_face_state) + IA_AIQ_MAX_NUM_FACES*sizeof(ia_face));
    if (m3aState.faces) {
        m3aState.faces->num_faces = 0;
        m3aState.faces->faces = (ia_face*)((char*)m3aState.faces + sizeof(ia_face_state));
    }
    else
        return -1;

    resetAFParams();
    mAfState.af_results = NULL;
    memset(&mAeState, 0, sizeof(mAeState));
    for (int i = 0; i <= AE_DELAY_FRAMES; i++) {
        mAeState.prev_results[i].exposure = &mAeState.prev_exposure[i];
        mAeState.prev_results[i].sensor_exposure = &mAeState.prev_sensor_exposure[i];
        mAeState.prev_results[i].flash = &mAeState.prev_flash[i];
    }
    resetAECParams();
    resetAWBParams();
    mAwbResults = NULL;
    resetGBCEParams();
    resetDSDParams();

    return 0;
}

/* returns false for error, true for success */
bool AtomAIQ::changeSensorMode(void)
{
    LOG1("@%s", __FUNCTION__);

    /* Get new sensor frame params needed by AIC for LSC calculation. */
    getSensorFrameParams(&m3aState.sensor_frame_params);

    struct atomisp_sensor_mode_data sensor_mode_data;
    mISP->sensorGetModeInfo(&sensor_mode_data);
    if (mISP->getIspParameters(&m3aState.results.isp_params) < 0)
        return false;

    /* Reconfigure 3A grid */
    ia_aiq_exposure_sensor_descriptor *sd = &mAeSensorDescriptor;
    sd->pixel_clock_freq_mhz = sensor_mode_data.vt_pix_clk_freq_mhz/1000000.0f;
    sd->pixel_periods_per_line = sensor_mode_data.line_length_pck;
    sd->line_periods_per_field = sensor_mode_data.frame_length_lines;
    sd->fine_integration_time_min = sensor_mode_data.fine_integration_time_def;
    sd->fine_integration_time_max_margin = sensor_mode_data.line_length_pck - sensor_mode_data.fine_integration_time_def;
    sd->coarse_integration_time_min = sensor_mode_data.coarse_integration_time_min;
    sd->coarse_integration_time_max_margin = sensor_mode_data.coarse_integration_time_max_margin;

    LOG2("sensor_descriptor assign complete: %d, %d", mAeInputParameters.sensor_descriptor->line_periods_per_field,
        sd->coarse_integration_time_max_margin);

    if (m3aState.stats)
        freeStatistics(m3aState.stats);

    m3aState.curr_grid_info = m3aState.results.isp_params.info;
    int grid_size = m3aState.curr_grid_info.s3a_width * m3aState.curr_grid_info.s3a_height;
    m3aState.stats = allocateStatistics(grid_size);
    if (m3aState.stats != NULL) {
        m3aState.stats->grid_info = m3aState.curr_grid_info;
        m3aState.stats_valid  = false;
    } else {
        return false;
    }

    return true;
}

status_t AtomAIQ::getStatistics(const struct timeval *frame_timestamp,
                                const struct timeval *sof_timestamp)
{
    LOG2("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    PERFORMANCE_TRACES_AAA_PROFILER_START();
    ret = mISP->getIspStatistics(m3aState.stats);
    if (ret == EAGAIN) {
        LOGV("buffer for isp statistics reallocated according resolution changing\n");
        if (changeSensorMode() == false)
            LOGE("error in calling changeSensorMode()\n");
        ret = mISP->getIspStatistics(m3aState.stats);
    }
    PERFORMANCE_TRACES_AAA_PROFILER_STOP();

    if (ret == 0)
    {
        ia_err err = ia_err_none;
        ia_aiq_statistics_input_params statistics_input_parameters;
        memset(&statistics_input_parameters, 0, sizeof(ia_aiq_statistics_input_params));

        long long eof_timestamp = (long long)((frame_timestamp->tv_sec*1000000000LL + frame_timestamp->tv_usec*1000LL)/1000LL);
        statistics_input_parameters.frame_timestamp = (unsigned long long)((sof_timestamp->tv_sec*1000000000LL + sof_timestamp->tv_usec*1000LL)/1000LL);
        if (eof_timestamp < (long long)statistics_input_parameters.frame_timestamp ||
            eof_timestamp - (long long)statistics_input_parameters.frame_timestamp > MAX_EOF_SOF_DIFF)
        {
            statistics_input_parameters.frame_timestamp = eof_timestamp - DEFAULT_EOF_SOF_DELAY;
        }

        statistics_input_parameters.external_histogram = NULL;

        if(m3aState.faces)
            statistics_input_parameters.faces = m3aState.faces;

        if(mAwbResults)
            statistics_input_parameters.frame_awb_parameters = mAwbResults;

        if (mAeState.ae_results)
            statistics_input_parameters.frame_ae_parameters = &mAeState.prev_results[0];

        statistics_input_parameters.wb_gains = NULL;
        statistics_input_parameters.cc_matrix = NULL;

        if ((mISP->getCssMajorVersion() == 1) && (mISP->getCssMinorVersion() == 5))
            ia_isp_1_5_statistics_convert(m3aState.ia_isp_handle, m3aState.stats,
                            const_cast<ia_aiq_rgbs_grid**>(&statistics_input_parameters.rgbs_grid),
                            const_cast<ia_aiq_af_grid**>(&statistics_input_parameters.af_grid));
        else if ((mISP->getCssMajorVersion() == 2) && (mISP->getCssMinorVersion() == 0))
            ia_isp_2_2_statistics_convert(m3aState.ia_isp_handle, m3aState.stats,
                                        const_cast<ia_aiq_rgbs_grid**>(&statistics_input_parameters.rgbs_grid),
                                        const_cast<ia_aiq_af_grid**>(&statistics_input_parameters.af_grid));

        LOG2("m3aState.stats: grid_info: %d  %d %d ",
              m3aState.stats->grid_info.s3a_width,m3aState.stats->grid_info.s3a_height,m3aState.stats->grid_info.s3a_bqs_per_grid_cell);

        LOG2("rgb_grid: grid_width:%u, grid_height:%u, thr_r:%u, thr_gr:%u,thr_gb:%u", statistics_input_parameters.rgbs_grid->grid_width,
              statistics_input_parameters.rgbs_grid->grid_height,
              statistics_input_parameters.rgbs_grid->blocks_ptr->avg_r,
              statistics_input_parameters.rgbs_grid->blocks_ptr->avg_g,
              statistics_input_parameters.rgbs_grid->blocks_ptr->avg_b);

        err = ia_aiq_statistics_set(m3aState.ia_aiq_handle, &statistics_input_parameters);

        m3aState.stats_valid = true;
    }

    return ret;
}

void AtomAIQ::setAfFocusMode(ia_aiq_af_operation_mode mode)
{
    mAfInputParameters.focus_mode = mode;
}

void AtomAIQ::setAfFocusRange(ia_aiq_af_range range)
{
    mAfInputParameters.focus_range = range;
}

void AtomAIQ::setAfMeteringMode(ia_aiq_af_metering_mode mode)
{
    mAfInputParameters.focus_metering_mode = mode;
}

void AtomAIQ::resetAFParams()
{
    LOG2("@%s", __FUNCTION__);
    mAfInputParameters.focus_mode = ia_aiq_af_operation_mode_auto;
    mAfInputParameters.focus_range = ia_aiq_af_range_extended;
    mAfInputParameters.focus_metering_mode = ia_aiq_af_metering_mode_auto;
    mAfInputParameters.flash_mode = ia_aiq_flash_mode_auto;

    mAfInputParameters.focus_rect = &mAfState.focus_rect;
    mAfInputParameters.focus_rect->height = 0;
    mAfInputParameters.focus_rect->width = 0;
    mAfInputParameters.focus_rect->left = 0;
    mAfInputParameters.focus_rect->top = 0;
    mAfInputParameters.frame_use = m3aState.frame_use;
    mAfInputParameters.lens_position = 0;

    mAfInputParameters.manual_focus_parameters = &mAfState.focus_parameters;
    mAfInputParameters.manual_focus_parameters->manual_focus_action = ia_aiq_manual_focus_action_none;
    mAfInputParameters.manual_focus_parameters->manual_focus_distance = 500;
    mAfInputParameters.manual_focus_parameters->manual_lens_position = 0;

    mAfState.af_locked = false;
    mAfState.aec_locked = false;

}

status_t AtomAIQ::runAfMain()
{
    LOG2("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    if (mAfState.af_locked)
        return ret;

    ia_err err = ia_err_none;

    LOG2("@af window = (%d,%d,%d,%d)",mAfInputParameters.focus_rect->height,
                 mAfInputParameters.focus_rect->width,
                 mAfInputParameters.focus_rect->left,
                 mAfInputParameters.focus_rect->top);

    if(m3aState.ia_aiq_handle)
        err = ia_aiq_af_run(m3aState.ia_aiq_handle, &mAfInputParameters, &mAfState.af_results);

    ia_aiq_af_results* af_results_ptr = mAfState.af_results;

    /* Move the lens to the required lens position */
    LOG2("lens_driver_action:%d", af_results_ptr->lens_driver_action);
    if (err == ia_err_none && af_results_ptr->lens_driver_action == ia_aiq_lens_driver_action_move_to_unit)
    {
        LOG2("next lens position:%ld", af_results_ptr->next_lens_position);
        ret = mISP->sensorMoveFocusToPosition(af_results_ptr->next_lens_position);
        if (ret == NO_ERROR)
        {
            clock_gettime(CLOCK_MONOTONIC, &m3aState.lens_timestamp);
            mAfInputParameters.lens_movement_start_timestamp = (unsigned long long)((m3aState.lens_timestamp.tv_sec*1000000000LL + m3aState.lens_timestamp.tv_nsec)/1000LL);
            mAfInputParameters.lens_position = af_results_ptr->next_lens_position; /*Assume that the lens has moved to the requested position*/
        }
    }
    return ret;
}

void AtomAIQ::resetAECParams()
{
    LOG2("@%s", __FUNCTION__);
    mAeMode = CAM_AE_MODE_NOT_SET;

    mAeInputParameters.frame_use = m3aState.frame_use;

    mAeInputParameters.flash_mode = ia_aiq_flash_mode_auto;
    mAeInputParameters.operation_mode = ia_aiq_ae_operation_mode_automatic;
    mAeInputParameters.metering_mode = ia_aiq_ae_metering_mode_evaluative;
    mAeInputParameters.priority_mode = ia_aiq_ae_priority_mode_normal;
    mAeInputParameters.flicker_reduction_mode = ia_aiq_ae_flicker_reduction_auto;
    mAeInputParameters.sensor_descriptor = &mAeSensorDescriptor;

    mAeInputParameters.exposure_coordinate = NULL;
    mAeInputParameters.ev_shift = 0;
    mAeInputParameters.manual_exposure_time_us = -1;
    mAeInputParameters.manual_analog_gain = -1;
    mAeInputParameters.manual_iso = -1;
    mAeInputParameters.manual_frame_time_us_min = -1;
    mAeInputParameters.manual_frame_time_us_max = -1;
    mAeInputParameters.aec_features = ia_aiq_ae_feature_tuning;
}

status_t AtomAIQ::runAeMain()
{
    LOG2("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    if (mAeState.ae_locked)
        return ret;

    // ToDo:
    // More intelligent handling of ae_lock should be implemented:
    // Use case when mode/resolution changes and AE lock is ON would not produce new/correct sensor exposure parameters.
    // Maybe AE should be run in manual mode with previous results to produce same exposure parameters but for different sensor mode?

    ia_err err = ia_err_none;
    ia_aiq_ae_results *new_ae_results = NULL;

    bool first_run = true;
    if (mAeState.ae_results)
        first_run = false;

    LOG2("AEC manual_exposure_time_us: %ld manual_analog_gain: %f manual_iso: %d", mAeInputParameters.manual_exposure_time_us, mAeInputParameters.manual_analog_gain, mAeInputParameters.manual_iso);
    LOG2("AEC sensor_descriptor ->line_periods_per_field: %d", mAeInputParameters.sensor_descriptor->line_periods_per_field);
    LOG2("AEC mAeInputParameters.frame_use: %d",mAeInputParameters.frame_use);

    if(m3aState.ia_aiq_handle){
        err = ia_aiq_ae_run(m3aState.ia_aiq_handle, &mAeInputParameters, &new_ae_results);
        LOG2("@%s result: %d", __FUNCTION__, err);
    }

    if (new_ae_results &&
        (first_run || new_ae_results->flash->status == ia_aiq_flash_status_pre))
    {
        /*
         * Fill AE results history with first AE results because there is no AE delay in the beginning OR
         * Fill AE results history with first AE results because there is no AE delay after mode change (handled with 'first_run' flag - see switchModeAndRate()) OR
         * Fill AE results history with flash AE results because flash process skips partially illuminated frames removing the AE delay.
         */
        for (int i = 1; i <= AE_DELAY_FRAMES; i++)
        {
            ia_aiq_ae_results *history_ae_results = &mAeState.prev_results[i];

            // TODO: Weight grid addresses are the same always. May change in the future.
            history_ae_results->weight_grid = new_ae_results->weight_grid;
            memcpy(history_ae_results->exposure, new_ae_results->exposure, sizeof(ia_aiq_exposure_parameters));
            memcpy(history_ae_results->sensor_exposure, new_ae_results->exposure, sizeof(ia_aiq_exposure_sensor_parameters));
            memcpy(history_ae_results->flash, new_ae_results->flash, sizeof(ia_aiq_flash_parameters));
        }
    }

    // TODO: Make sure exposure parameters are not moved in the list more than once per frame (ie. if AEC is called multiple times per frame).
    for (int i = 0; i < AE_DELAY_FRAMES; i++)
    {
        ia_aiq_ae_results *old_ae_results = &mAeState.prev_results[i+1];
        ia_aiq_ae_results *older_ae_results = &mAeState.prev_results[i];

        older_ae_results->weight_grid = old_ae_results->weight_grid;
        memcpy(older_ae_results->exposure, old_ae_results->exposure, sizeof(ia_aiq_exposure_parameters));
        memcpy(older_ae_results->sensor_exposure, old_ae_results->sensor_exposure, sizeof(ia_aiq_exposure_sensor_parameters));
        memcpy(older_ae_results->flash, old_ae_results->flash, sizeof(ia_aiq_flash_parameters));
    }

    if (new_ae_results != NULL)
    {
        ia_aiq_ae_results *prev_ae_results = &mAeState.prev_results[AE_DELAY_FRAMES];

        // Compare sensor exposure parameters instead of generic exposure parameters to take into account mode changes when exposure time doesn't change but sensor parameters do change.
        if (prev_ae_results->sensor_exposure->coarse_integration_time != new_ae_results->sensor_exposure->coarse_integration_time ||
            prev_ae_results->sensor_exposure->fine_integration_time != new_ae_results->sensor_exposure->fine_integration_time ||
            prev_ae_results->sensor_exposure->digital_gain_global != new_ae_results->sensor_exposure->digital_gain_global ||
            prev_ae_results->sensor_exposure->analog_gain_code_global != new_ae_results->sensor_exposure->analog_gain_code_global)
        {
            mAeState.exposure.integration_time[0] = new_ae_results->sensor_exposure->coarse_integration_time;
            mAeState.exposure.integration_time[1] = new_ae_results->sensor_exposure->fine_integration_time;
            mAeState.exposure.gain[0] = new_ae_results->sensor_exposure->analog_gain_code_global;
            mAeState.exposure.gain[1] = new_ae_results->sensor_exposure->digital_gain_global;

            mAeState.exposure.aperture = 100;

            LOG2("AEC integration_time[0]: %d", mAeState.exposure.integration_time[0]);
            LOG2("AEC integration_time[1]: %d", mAeState.exposure.integration_time[1]);
            LOG2("AEC gain[0]: %x", mAeState.exposure.gain[0]);
            LOG2("AEC gain[1]: %x", mAeState.exposure.gain[1]);
            LOG2("AEC aperture: %d\n", mAeState.exposure.aperture);

            /* Apply Sensor settings */
            ret |= mISP->sensorSetExposure(&mAeState.exposure);
        }

        // TODO: Verify that checking the power change is enough. Should status be checked (rer/pre/main).
        if (prev_ae_results->flash->power_prc != new_ae_results->flash->power_prc)
        {
            /* Apply Flash settings */
            if (mAeState.ae_results)
                ret |= mISP->setFlashIntensity((int)(mAeState.ae_results->flash)->power_prc);
            else
                LOGE("ae_results is NULL, could not apply flash settings");
        }

        // Store the latest AE results in the end if the list.
        prev_ae_results->weight_grid = new_ae_results->weight_grid;
        memcpy(prev_ae_results->exposure, new_ae_results->exposure, sizeof(ia_aiq_exposure_parameters));
        memcpy(prev_ae_results->sensor_exposure, new_ae_results->sensor_exposure, sizeof(ia_aiq_exposure_sensor_parameters));
        memcpy(prev_ae_results->flash, new_ae_results->flash, sizeof(ia_aiq_flash_parameters));

        mAeState.ae_results = new_ae_results;
    }
    return ret;
}

void AtomAIQ::resetAWBParams()
{
    LOG2("@%s", __FUNCTION__);
    mAwbInputParameters.frame_use = m3aState.frame_use;
    mAwbInputParameters.scene_mode = ia_aiq_awb_operation_mode_auto;
    mAwbInputParameters.manual_cct_range = NULL;
    mAwbInputParameters.manual_white_coordinate = NULL;
}

void AtomAIQ::runAwbMain()
{
    LOG2("@%s", __FUNCTION__);

    if (mAwbLocked)
        return;
    ia_err ret = ia_err_none;

    if(m3aState.ia_aiq_handle)
    {
        //mAwbInputParameters.scene_mode = ia_aiq_awb_operation_mode_auto;
        LOG2("before ia_aiq_awb_run() param-- frame_use:%d scene_mode:%d", mAwbInputParameters.frame_use, mAwbInputParameters.scene_mode);
        ret = ia_aiq_awb_run(m3aState.ia_aiq_handle, &mAwbInputParameters, &mAwbResults);
        LOG2("@%s result: %d", __FUNCTION__, ret);
    }
}

void AtomAIQ::resetGBCEParams()
{
    mGBCEEnable = true;
    mGBCEResults = NULL;
}

status_t AtomAIQ::runGBCEMain()
{
    LOG2("@%s", __FUNCTION__);
    if (m3aState.ia_aiq_handle && mGBCEEnable) {
        ia_err err = ia_aiq_gbce_run(m3aState.ia_aiq_handle, &mGBCEResults);
        if(err == ia_err_none)
            LOG2("@%s success", __FUNCTION__);
    } else {
        mGBCEResults = NULL;
    }
    return NO_ERROR;
}


void AtomAIQ::resetDSDParams()
{
    m3aState.dsd_enabled = false;
}

status_t AtomAIQ::runDSDMain()
{
    LOG2("@%s", __FUNCTION__);
    if (m3aState.ia_aiq_handle && m3aState.dsd_enabled)
    {
        mDSDInputParameters.af_results = mAfState.af_results;
        ia_err ret = ia_aiq_dsd_run(m3aState.ia_aiq_handle, &mDSDInputParameters, &mDetectedSceneMode);
        if(ret == ia_err_none)
            LOG2("@%s success, detected scene mode: %d", __FUNCTION__, mDetectedSceneMode);
    }
    return NO_ERROR;
}

status_t AtomAIQ::run3aMain()
{
    LOG2("@%s", __FUNCTION__);
    status_t ret = NO_ERROR;

    if(!mISP->isFileInjectionEnabled())
        ret |= runAfMain();

    // if no DSD enable, should disable that
    if(!mISP->isFileInjectionEnabled())
        ret |= runDSDMain();

    if(!mISP->isFileInjectionEnabled())
        ret |= runAeMain();

    runAwbMain();

    if(mAeMode != CAM_AE_MODE_MANUAL)
        ret |= runGBCEMain();
    else
        mGBCEResults = NULL;

    // get AIC result and apply into ISP
    ret |= runAICMain();

    return ret;
}
/*
int AtomAIQ::run3aMain()
{
    LOG2("@%s", __FUNCTION__);
    int ret = 0;
    //
    switch(mFlashStage) {
    case CAM_FLASH_STAGE_AF:
        mAfInputParameters.frame_use = ia_aiq_frame_use_still;
        runAfMain();
        if(! mAfState.af_results->status == ia_aiq_af_status_success &&
           ! mAfState.af_results->status == ia_aiq_af_status_fail)
        //need more frame for AF run
            return -1;
        mFlashStage = CAM_FLASH_STAGE_AE;
        if(m3aState.dsd_enabled)
            runDSDMain();
        if(mAfState.af_results->use_af_assist) {
            LOG2("af assist on, need to set off");
            mAfInputParameters.flash_mode = ia_aiq_flash_mode_off;
            //need more frame for AEC run
            return -1;
        }
        return -1;
        break;
    case CAM_FLASH_STAGE_AE:
        ret = AeForFlash();
        if(ret < 0)
            return ret;
        mFlashStage = CAM_FLASH_STAGE_AF;
        break;
    default:
        LOG2("flash stage finished");
    }

    runAwbMain();
    if(mGBCEEnable && mAeMode != CAM_AE_MODE_MANUAL)
        runGBCEMain();

    // get AIC result and apply into ISP
    runAICMain();

    return 0;
}


//run 3A for flash
int AtomAIQ::AeForFlash()
{
    LOG2("@%s", __FUNCTION__);

    runAeMain();
    if (mAeState.ae_results->flash->status == ia_aiq_flash_status_no) {
        if(!mAeState.ae_results->converged) {
            mAeInputParameters.frame_use = ia_aiq_frame_use_preview;
            mAeInputParameters.flash_mode = ia_aiq_flash_mode_off;
            //need one more frame for AE
            LOG2("@%s need one more frame for AE when result not converged with flash off", __FUNCTION__);
            return -1;
        }
        if(mAeState.ae_results->converged) {
            m3aState.frame_use = ia_aiq_frame_use_still;
            runAeMain();
            LOG2("flash off and converged");
            return 0;
        }
    }else if (mAeState.ae_results->flash->status ==  ia_aiq_flash_status_torch) {
            m3aState.frame_use = ia_aiq_frame_use_still;
            runAeMain();
            if(mAeState.ae_results->flash->status == ia_aiq_flash_status_pre) {
                mAeInputParameters.flash_mode = ia_aiq_flash_mode_on;
                LOG2("@%s need one more frame for AE when result in pre with flash on", __FUNCTION__);
                return -1;
            }
            if (mAeState.ae_results->flash->status == ia_aiq_flash_status_main) {
                LOG2("@%s go ahead for capturing, it's main stage", __FUNCTION__);
                return 0;
            }
    }

    return NO_ERROR;
}
*/
status_t AtomAIQ::runAICMain()
{
    LOG2("@%s", __FUNCTION__);

    status_t ret = NO_ERROR;

    if (m3aState.ia_aiq_handle) {
        ia_aiq_pa_input_params pa_input_params;

        // NOTE: currently the input parameter structs are identical for CSS 1.5 and 2.0
        // To reduce lots of if elses, the parameters are first stored into a 1.5 version
        // A more intelligent way needs to be figured out. Such as hiding the CSS
        // differencies into AIQ library.
        ia_isp_1_5_input_params isp_15_input_params;

        pa_input_params.frame_use = m3aState.frame_use;
        isp_15_input_params.frame_use = m3aState.frame_use;

        pa_input_params.awb_results = NULL;
        isp_15_input_params.awb_results = NULL;

        isp_15_input_params.exposure_results = (mAeState.ae_results) ? mAeState.ae_results->exposure : NULL;

        if (mAwbResults) {
            LOG2("awb factor:%f", mAwbResults->accurate_b_per_g);
        }
        pa_input_params.awb_results = mAwbResults;
        isp_15_input_params.awb_results = mAwbResults;

        if (mGBCEResults) {
            LOG2("gbce :%d", mGBCEResults->ctc_gains_lut_size);
        }
        isp_15_input_params.gbce_results = mGBCEResults;

        pa_input_params.sensor_frame_params = &m3aState.sensor_frame_params;
        isp_15_input_params.sensor_frame_params = &m3aState.sensor_frame_params;
        LOG2("@%s  2 sensor native width %d", __FUNCTION__, pa_input_params.sensor_frame_params->cropped_image_width);

        pa_input_params.cc_matrix = NULL;
        pa_input_params.wb_gains = NULL;

        ia_aiq_pa_results *pa_results;
        ret = ia_aiq_pa_run(m3aState.ia_aiq_handle, &pa_input_params, &pa_results);
        LOG2("@%s  ia_aiq_pa_run :%d", __FUNCTION__, ret);

        isp_15_input_params.pa_results = pa_results;

        if ((mISP->getCssMajorVersion() == 1) && (mISP->getCssMinorVersion() ==5))
            isp_15_input_params.effects = mISP15InputParameters.effects;
        else if ((mISP->getCssMajorVersion() == 2) && (mISP->getCssMinorVersion() ==0))
            isp_15_input_params.effects = mISP22InputParameters.effects;

        isp_15_input_params.manual_brightness = 0;
        isp_15_input_params.manual_contrast = 0;
        isp_15_input_params.manual_hue = 0;
        isp_15_input_params.manual_saturation = 0;
        isp_15_input_params.manual_sharpness = 0;

        int value = 0;
        PlatformData::HalConfig.getValue(value, CPF::IspVamemType);
        isp_15_input_params.isp_vamem_type = value;

        if ((mISP->getCssMajorVersion() == 1) && (mISP->getCssMinorVersion() == 5)) {
            ret = ia_isp_1_5_run(m3aState.ia_isp_handle, &isp_15_input_params, &((m3aState.results).isp_output));
            LOG2("@%s  ia_isp_1_5_run :%d", __FUNCTION__, ret);
        }
        else if ((mISP->getCssMajorVersion() == 2) && (mISP->getCssMinorVersion() == 0)) {
            ia_isp_2_2_input_params isp_22_input_params;

            isp_22_input_params.frame_use = isp_15_input_params.frame_use;
            isp_22_input_params.sensor_frame_params = isp_15_input_params.sensor_frame_params;
            isp_22_input_params.exposure_results = isp_15_input_params.exposure_results;
            isp_22_input_params.awb_results = isp_15_input_params.awb_results;
            isp_22_input_params.gbce_results = isp_15_input_params.gbce_results;
            isp_22_input_params.pa_results = isp_15_input_params.pa_results;
            isp_22_input_params.isp_vamem_type = isp_15_input_params.isp_vamem_type;
            isp_22_input_params.manual_brightness = isp_15_input_params.manual_brightness;
            isp_22_input_params.manual_contrast = isp_15_input_params.manual_contrast;
            isp_22_input_params.manual_hue = isp_15_input_params.manual_hue;
            isp_22_input_params.manual_saturation = isp_15_input_params.manual_saturation;
            isp_22_input_params.manual_sharpness = isp_15_input_params.manual_sharpness;
            isp_22_input_params.effects = isp_15_input_params.effects;

            ret = ia_isp_2_2_run(m3aState.ia_isp_handle, &isp_22_input_params, &((m3aState.results).isp_output));
            LOG2("@%s  ia_isp_2_2_run :%d", __FUNCTION__, ret);
        }

        /* Apply ISP settings */
        if (m3aState.results.isp_output.data) {
            struct atomisp_parameters *aic_out_struct = (struct atomisp_parameters *)m3aState.results.isp_output.data;
            ret |= mISP->setAicParameter(aic_out_struct);
            ret |= mISP->applyColorEffect();
        }

        if (mISP->isFileInjectionEnabled() && ret == 0 && mAwbResults != NULL) {
            // When the awb result converged, and reach the max try count,
            // dump the makernote into file
            if ((mAwbResults->distance_from_convergence >= -EPSILON &&
                 mAwbResults->distance_from_convergence <= EPSILON) &&
                 mAwbRunCount > RETRY_COUNT ) {
                mAwbRunCount = 0;
                dumpMknToFile();
            } else if(mAwbResults->distance_from_convergence >= -EPSILON &&
                      mAwbResults->distance_from_convergence <= EPSILON){
                mAwbRunCount++;
                LOG2("AWB converged:%d", mAwbRunCount);
            }
        }
    }
    return ret;
}

int AtomAIQ::dumpMknToFile()
{
    LOG1("@%s", __FUNCTION__);
    FILE *fp;
    size_t bytes;
    String8 fileName;
    //get binary of makernote and store
    ia_3a_mknote *aaaMkNote;
    aaaMkNote = get3aMakerNote(ia_3a_mknote_mode_raw);
    if(aaaMkNote) {
        fileName = mISP->getFileInjectionFileName();
        fileName += ".mkn";
        LOG2("filename:%s",  fileName.string());
        fp = fopen (fileName.string(), "w+");
        if (fp == NULL) {
            LOGE("open file %s failed %s", fileName.string(), strerror(errno));
            put3aMakerNote(aaaMkNote);
            return -1;
        }
        if ((bytes = fwrite(aaaMkNote->data, aaaMkNote->bytes, 1, fp)) < (size_t)aaaMkNote->bytes)
            LOGW("Write less mkn bytes to %s: %d, %d", fileName.string(), aaaMkNote->bytes, bytes);
        fclose (fp);
    }
    return 0;
}

int AtomAIQ::enableFpn(bool enable)
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
void AtomAIQ::getAeExpCfg(int *exp_time,
                          short unsigned int *aperture_num,
                          short unsigned int *aperture_denum,
                     int *aec_apex_Tv, int *aec_apex_Sv, int *aec_apex_Av,
                     float *digital_gain)
{
    LOG2("@%s", __FUNCTION__);

    mISP->sensorGetExposureTime(exp_time);
    mISP->sensorGetFNumber(aperture_num, aperture_denum);
    if(mAeState.prev_results[AE_DELAY_FRAMES].exposure != NULL) {
        *digital_gain = (mAeState.prev_results[AE_DELAY_FRAMES].exposure)->digital_gain;
        *aec_apex_Tv = -1.0 * (log10((double)(mAeState.prev_results[AE_DELAY_FRAMES].exposure)->exposure_time_us/1000000) / log10(2.0)) * 65536;
        *aec_apex_Av = log10(pow((mAeState.prev_results[AE_DELAY_FRAMES].exposure)->aperture_fn, 2))/log10(2.0) * 65536;
        *aec_apex_Sv = log10(pow(2.0, -7.0/4.0) * (mAeState.prev_results[AE_DELAY_FRAMES].exposure)->iso) / log10(2.0) * 65536;
    }
}

void AtomAIQ::getDefaultParams(CameraParameters *params, CameraParameters *intel_params)
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

    intel_params->set(IntelCameraParameters::KEY_HDR_IMAGING, FeatureData::hdrDefault(cameraId));
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_HDR_IMAGING, FeatureData::hdrSupported(cameraId));
    intel_params->set(IntelCameraParameters::KEY_HDR_VIVIDNESS, "gaussian");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_HDR_VIVIDNESS, "none,gaussian,gamma");
    intel_params->set(IntelCameraParameters::KEY_HDR_SHARPENING, "normal");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_HDR_SHARPENING, "none,normal,strong");
    intel_params->set(IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL, "off");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_HDR_SAVE_ORIGINAL, "on,off");

    // back lighting correction mode
    intel_params->set(IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE, "off");
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_BACK_LIGHTING_CORRECTION_MODES, "on,off");

    // AWB mapping mode
    intel_params->set(IntelCameraParameters::KEY_AWB_MAPPING_MODE, IntelCameraParameters::AWB_MAPPING_AUTO);
    intel_params->set(IntelCameraParameters::KEY_SUPPORTED_AWB_MAPPING_MODES, "auto,indoor,outdoor");

    // panorama
    intel_params->set(IntelCameraParameters::KEY_PANORAMA_LIVE_PREVIEW_SIZE, CAM_RESO_STR(PANORAMA_DEF_PREV_WIDTH,PANORAMA_DEF_PREV_HEIGHT));

}

void AtomAIQ::getSensorFrameParams(ia_aiq_sensor_frame_params *frame_params)
{
    LOG2("@%s", __FUNCTION__);

    struct atomisp_sensor_mode_data sensor_mode_data;
    if(mISP->sensorGetModeInfo(&sensor_mode_data) < 0) {
        sensor_mode_data.crop_horizontal_start = 0;
        sensor_mode_data.crop_vertical_start = 0;
        sensor_mode_data.crop_vertical_end = 0;
        sensor_mode_data.crop_horizontal_end = 0;
    }
    frame_params->horizontal_crop_offset = sensor_mode_data.crop_horizontal_start;
    frame_params->vertical_crop_offset = sensor_mode_data.crop_vertical_start;
    frame_params->cropped_image_height = sensor_mode_data.crop_vertical_end - sensor_mode_data.crop_vertical_start;
    frame_params->cropped_image_width = sensor_mode_data.crop_horizontal_end - sensor_mode_data.crop_horizontal_start;
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
                sensor_mode_data.output_width * 254 * sensor_mode_data.binning_factor_x/ frame_params->cropped_image_width;
        frame_params->vertical_scaling_numerator =
                sensor_mode_data.output_height * 254 * sensor_mode_data.binning_factor_y / frame_params->cropped_image_height;
    }
}

} //  namespace android
