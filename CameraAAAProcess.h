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

#ifndef ANDROID_HARDWARE_CAMERA_AAA_PROCESS_H
#define ANDROID_HARDWARE_CAMERA_AAA_PROCESS_H

#include <utils/threads.h>

#ifdef __cplusplus
extern "C" {
#endif
#include "ci_adv_pub.h"
#include "ci_adv_property.h"
#include "atomisp_config.h"

#ifdef __cplusplus
}
#endif

namespace android {

typedef enum
{
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
} cam_awb_mode_t;

typedef enum
{
    CAM_AWB_MAP_AUTO,
    CAM_AWB_MAP_INDOOR,
    CAM_AWB_MAP_OUTDOOR
} cam_awb_map_mode;

typedef enum
{
    CAM_AF_MODE_AUTO,
    CAM_AF_MODE_MACRO,
    CAM_AF_MODE_INFINITY,
    CAM_AF_MODE_TOUCH,
    CAM_AF_MODE_MANUAL
} cam_af_mode_t;

typedef enum
{
    CAM_AF_METERING_MODE_AUTO,
    CAM_AF_METERING_MODE_SPOT,
} cam_af_metering_mode;

typedef enum {
    CAM_AE_FLICKER_MODE_OFF,
    CAM_AE_FLICKER_MODE_50HZ,
    CAM_AE_FLICKER_MODE_60HZ,
    CAM_AE_FLICKER_MODE_AUTO
} cam_ae_flicker_mode_t;

typedef enum
{
    CAM_AE_FLASH_MODE_AUTO,
    CAM_AE_FLASH_MODE_OFF,
    CAM_AE_FLASH_MODE_ON,
    CAM_AE_FLASH_MODE_DAY_SYNC,
    CAM_AE_FLASH_MODE_SLOW_SYNC,
    CAM_AE_FLASH_MODE_TORCH
} cam_ae_flash_mode_t;

typedef enum
{
    CAM_AE_SCENE_MODE_AUTO,
    CAM_AE_SCENE_MODE_PORTRAIT,
    CAM_AE_SCENE_MODE_SPORTS,
    CAM_AE_SCENE_MODE_LANDSCAPE,
    CAM_AE_SCENE_MODE_NIGHT,
    CAM_AE_SCENE_MODE_FIREWORKS
} cam_ae_scene_mode_t;

typedef enum
{
    CAM_AE_MODE_AUTO,
    CAM_AE_MODE_MANUAL,
    CAM_AE_MODE_SHUTTER_PRIORITY,
    CAM_AE_MODE_APERTURE_PRIORITY
} cam_ae_mode;

typedef enum
{
    CAM_AE_METERING_MODE_AUTO,
    CAM_AE_METERING_MODE_SPOT,
    CAM_AE_METERING_MODE_CENTER,
    CAM_AE_METERING_MODE_CUSTOMIZED
} cam_ae_metering_mode;

typedef enum
{
    CAM_FLASH_STAGE_NONE,
    CAM_FLASH_STAGE_PRE,
    CAM_FLASH_STAGE_MAIN
} cam_flash_stage;

typedef struct {
    int x_left;
    int x_right;
    int y_top;
    int y_bottom;
    int     weight;
} cam_Window;


#define AAA_FAIL    1
#define AAA_SUCCESS 0

class AAAProcess {
public:
    AAAProcess(int sensortype);
    ~AAAProcess();

    void Init(int sensor);
    void Uninit(void);

    void IspSetFd(int fd);

    void SwitchMode(int mode);
    void SetFrameRate(float framerate);

    void AeAfAwbProcess(bool read_stats);

    int DisReadStatistics(void);
    void DisProcess(void);
    void DisUpdateResults(void);
    void SetDisVector(void);

    void AfStillStart(void);
    void AfStillStop(void);
    int AfStillIsComplete(bool *complete);

    int PreFlashProcess(cam_flash_stage stage);

    void SetStillStabilizationEnabled(bool en);
    void GetStillStabilizationEnabled (bool * en);
    void DisCalcStill(ci_adv_dis_vector *vector, int frame_number);
    void StillCompose(ci_adv_user_buffer *com_buf,
                      ci_adv_user_buffer bufs[], int frame_dis, ci_adv_dis_vector vectors[]);

    int SetRedEyeRemoval(bool en);
    int GetRedEyeRemoval(bool *en);
    void DoRedeyeRemoval(void *img_buf, int size, int width, int height, int format);

    void LoadGdcTable(void);

    int AeSetMode(int mode);
    int AeGetMode(int *mode);
    int AeSetSceneMode(int mode);
    int AeGetSceneMode(int *mode);
    int AeSetMeteringMode(int mode);
    int AeGetMeteringMode(int *mode);
    int AeSetEv(float bias);
    int AeGetEv(float *bias);
    int AeSetFlashMode(int mode);
    int AeGetFlashMode(int *mode);
    int AeIsFlashNecessary(bool *used);
    int AeSetFlickerMode(int mode);
    int AeGetFlickerMode(int *mode);
    int AeSetBacklightCorrection(bool en);
    int AeGetBacklightCorrection(bool *en);
    int AeGetExpCfg(unsigned short *exp_time,
                    unsigned short *aperture);
    int AeSetWindow(const cam_Window *window);
    int AeGetWindow(cam_Window *window);
    int AeLock(bool lock);
    int AeIsLocked(bool *lock);

    int FlushManualSettings(void);
    int AeSetManualIso(int sensitivity, bool to_hw);
    int AeGetManualIso(int *sensitivity);
    int AeSetManualAperture(float aperture, bool to_hw);
    int AeGetManualAperture(float *aperture);
    int AeGetManualBrightness(float *brightness);
    int AeSetManualShutter(float exp_time, bool to_hw);
    int AeGetManualShutter(float *exp_time);

    int AfSetManualFocus(int focus, bool to_hw);
    int AfGetManualFocus(int *focus);
    int AfGetFocus(int *focus);

    int AfSetMode(int mode);
    int AfGetMode(int *mode);
    int AfSetMeteringMode(int mode);
    int AfGetMeteringMode(int *mode);
    int AfSetWindow(const cam_Window *window);
    int AfGetWindow(cam_Window *window);

    int AwbSetMode (int wb_mode);
    int AwbGetMode(int *wb_mode);
    int AwbSetManualColorTemperature(int ct, bool to_hw);
    int AwbGetManualColorTemperature(int *ct);
    int AwbSetMapping(int mode);
    int AwbGetMapping(int *mode);

    void SetAfEnabled(bool enabled);
    void SetAeEnabled(bool enabled);
    void SetAwbEnabled(bool enabled);

    void SetGdcEnabled(bool enabled){
        mGdcEnabled = enabled;
    }

    bool GetAfEnabled(void) {
        return ci_adv_af_is_enabled();
    }
    bool GetAeEnabled(void) {
        return ci_adv_ae_is_enabled();
    }
    bool GetAwbEnabled(void) {
        return ci_adv_awb_is_enabled();
    }
    bool GetGdcEnabled(void) {
        return mGdcEnabled;
    }

    unsigned int GetAfStillFrames() {
        return mAfStillFrames;
    }

    void SetAfStillFrames(unsigned int frames) {
        mAfStillFrames = frames;
    }

#define AF_STILL_MAX_FRAMES 100
    unsigned int GetAfStillIsOverFrames() {
        return (mAfStillFrames >= AF_STILL_MAX_FRAMES);
    }
    ci_adv_dis_vector   dvs_vector;

    bool GetDoneStatisticsState(void) {
        return mDoneStatistics;
    }

    void SetDoneStatisticsState(bool val) {
        mDoneStatistics = val;
    }

private:
    /* not 0 is enabled, 0 is disabled */
    bool mGdcEnabled;
    mutable Mutex       mLock;

    unsigned int mAeMode;
    unsigned int mAwbMode;
    unsigned int mAfMode;
    int mFocusPosition;
    unsigned int mColorTemperature;
    float mManualAperture;
    float mManualShutter;
    int mManualIso;

    unsigned int mSensorType;
    int main_fd;

    //static const unsigned int mAfStillMaxFrames = 500;
    unsigned int  mAfStillFrames;  // 100 frames will time out

    bool mDoneStatistics;   // true means the statistics has been get

    bool mInitied;    // 0 means not init, not 0 means has been initied.
};


}; // namespace android

#endif // ANDROID_HARDWARE_CAMERA_AAA_PROCESS_H
