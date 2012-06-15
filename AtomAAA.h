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

#ifndef ANDROID_LIBCAMERA_ATOM_AAA
#define ANDROID_LIBCAMERA_ATOM_AAA

#include <utils/Errors.h>
#include <utils/threads.h>
#include <time.h>
#include "AtomCommon.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "ci_adv_pub.h"
#include "ci_adv_property.h"
#ifdef __cplusplus
}
#endif
namespace android {

enum AwbMode
{
    CAM_AWB_MODE_NOT_SET = -1,
    CAM_AWB_MODE_AUTO,
    CAM_AWB_MODE_MANUAL_INPUT,
    CAM_AWB_MODE_DAYLIGHT,
    CAM_AWB_MODE_SUNSET,
    CAM_AWB_MODE_CLOUDY,
    CAM_AWB_MODE_TUNGSTEN,
    CAM_AWB_MODE_FLUORESCENT,
    CAM_AWB_MODE_WARM_FLUORESCENT,
    CAM_AWB_MODE_SHADOW,
    CAM_AWB_MODE_WARM_INCANDESCENT
};


enum AfMode
{
    CAM_AF_MODE_NOT_SET = -1,
    CAM_AF_MODE_AUTO,
    CAM_AF_MODE_MACRO,
    CAM_AF_MODE_INFINITY,
    CAM_AF_MODE_TOUCH,
    CAM_AF_MODE_MANUAL
};

enum FlickerMode
{
    CAM_AE_FLICKER_MODE_NOT_SET = -1,
    CAM_AE_FLICKER_MODE_OFF,
    CAM_AE_FLICKER_MODE_50HZ,
    CAM_AE_FLICKER_MODE_60HZ,
    CAM_AE_FLICKER_MODE_AUTO
};

enum FlashMode
{
    CAM_AE_FLASH_MODE_NOT_SET = -1,
    CAM_AE_FLASH_MODE_AUTO,
    CAM_AE_FLASH_MODE_OFF,
    CAM_AE_FLASH_MODE_ON,
    CAM_AE_FLASH_MODE_DAY_SYNC,
    CAM_AE_FLASH_MODE_SLOW_SYNC,
    CAM_AE_FLASH_MODE_TORCH
};

// DetermineFlash: returns true if flash should be determined according to current exposure
#define DetermineFlash(x) (x == CAM_AE_FLASH_MODE_AUTO || \
                           x == CAM_AE_FLASH_MODE_DAY_SYNC || \
                           x == CAM_AE_FLASH_MODE_SLOW_SYNC) \

enum SceneMode
{
    CAM_AE_SCENE_MODE_NOT_SET = -1,
    CAM_AE_SCENE_MODE_AUTO,
    CAM_AE_SCENE_MODE_PORTRAIT,
    CAM_AE_SCENE_MODE_SPORTS,
    CAM_AE_SCENE_MODE_LANDSCAPE,
    CAM_AE_SCENE_MODE_NIGHT,
    CAM_AE_SCENE_MODE_NIGHT_PORTRAIT,
    CAM_AE_SCENE_MODE_FIREWORKS,
    CAM_AE_SCENE_MODE_TEXT
};

enum AeMode
{
    CAM_AE_MODE_NOT_SET = -1,
    CAM_AE_MODE_AUTO,
    CAM_AE_MODE_MANUAL,
    CAM_AE_MODE_SHUTTER_PRIORITY,
    CAM_AE_MODE_APERTURE_PRIORITY
};

enum MeteringMode
{
    CAM_AE_METERING_MODE_NOT_SET = -1,
    CAM_AE_METERING_MODE_AUTO,
    CAM_AE_METERING_MODE_SPOT,
    CAM_AE_METERING_MODE_CENTER,
    CAM_AE_METERING_MODE_CUSTOMIZED
};

enum FlashStage
{
    CAM_FLASH_STAGE_NOT_SET = -1,
    CAM_FLASH_STAGE_NONE,
    CAM_FLASH_STAGE_PRE,
    CAM_FLASH_STAGE_MAIN
};

#define DEFAULT_GBCE            true
#define DEFAULT_GBCE_STRENGTH   0
#define MAX_TIME_FOR_AF         2000 // milliseconds
#define TORCH_INTENSITY         20   // 20%
#define EV_LOWER_BOUND         -100
#define EV_UPPER_BOUND          100

struct IspSettings
{
    int  GBCE_strength; // default: 0,  >0 -> stronger GBCE
    bool GBCE_enabled;
    bool inv_gamma;    // inversed gamma flag, used in negative effect
};

struct SensorParams
{
    float evBias;
    int expTime;
    int aperture;
    int aecApexTv;
    int aecApexSv;
    int aecApexAv;
    float digitalGain;
};

struct CiUserBuffer {
    ia_frame *ciMainBuf;
    ia_frame *ciPostviewBuf;
    int **cdf;
    size_t ciBufNum;
};

class AtomAAA {

// constructor/destructor
private:
    static AtomAAA* mInstance;
    AtomAAA();
public:
    static AtomAAA* getInstance() {
        if (mInstance == NULL) {
            mInstance = new AtomAAA();
        }
        return mInstance;
    }
    ~AtomAAA();

    bool is3ASupported() { return mHas3A; }

    // Initialization functions
    status_t init(const char *sensor_id, int fd, const char *otpInjectFile = NULL);
    status_t unInit();
    status_t applyIspSettings();
    status_t switchModeAndRate(AtomMode mode, float fps);

    // Getters and Setters
    status_t setAeWindow(const CameraWindow *window);
    status_t setAfWindow(const CameraWindow *window);
    status_t setAeFlickerMode(FlickerMode mode);
    status_t setAfEnabled(bool en);
    status_t setAeSceneMode(SceneMode mode);
    SceneMode getAeSceneMode();
    status_t setAeMode(AeMode mode);
    AeMode getAeMode();
    status_t setAfMode(AfMode mode);
    AfMode getAfMode();
    status_t setAeFlashMode(FlashMode mode);
    FlashMode getAeFlashMode();
    bool getAeFlashNecessary();
    status_t setAwbMode(AwbMode mode);
    AwbMode getAwbMode();
    status_t setAeMeteringMode(MeteringMode mode);
    MeteringMode getAeMeteringMode();
    status_t setAeBacklightCorrection(bool en);
    status_t setAeLock(bool en);
    bool     getAeLock();
    status_t setAfLock(bool en);
    bool     getAfLock();
    status_t setAwbLock(bool en);
    bool     getAwbLock();
    status_t setAwbMapping(ia_3a_awb_map mode);
    ia_3a_awb_map getAwbMapping();
    size_t   getAfMaxNumWindows();
    status_t setAfWindows(const CameraWindow *windows, size_t numWindows);
    status_t setNegativeEffect(bool en);
    status_t getExposureInfo(SensorParams& sensorParams);
    status_t getAeManualBrightness(float *ret);
    status_t setManualFocus(int focus, bool applyNow);
    status_t setManualFocusIncrement(int step);
    status_t updateManualFocus();
    status_t getAfLensPosRange(ia_3a_af_lens_range *lens_range);
    status_t getNextFocusPosition(int *pos);
    status_t getCurrentFocusPosition(int *pos);
    status_t applyEv(float bias);
    status_t setEv(float bias);
    status_t getEv(float *ret);
    status_t setManualIso(int ret);
    status_t getManualIso(int *ret);
    status_t setManualShutter(float expTime);
    status_t getManualShutter(float *expTime);

    // ISP processing functions
    status_t applyDvsProcess();
    status_t apply3AProcess(bool read_stats,
        struct timeval capture_timestamp);
    status_t computeCDF(const CiUserBuffer& inputBuf, size_t bufIndex);
    status_t composeHDR(const CiUserBuffer& inputBuf, const CiUserBuffer& outputBuf, unsigned vividness, unsigned sharpening);

    status_t startStillAf();
    status_t stopStillAf();
    ia_3a_af_status isStillAfComplete();
    status_t applyPreFlashProcess(FlashStage stage);

// private methods
private:

// private members
private:

    struct IspSettings mIspSettings;   // ISP related settings
    Mutex m3aLock;
    int mIspFd;
    bool mHas3A;
    SensorType mSensorType;
    AfMode mAfMode;
    FlashMode mFlashMode;
    AwbMode mAwbMode;
    int mFocusPosition;
    nsecs_t mStillAfStart;
}; // class AtomAAA

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_AAA
