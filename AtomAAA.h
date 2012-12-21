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

// Forward declaration needed to resolve circular reference between AtomAAA
// and AtomISP objects.
namespace android {

enum FlickerMode
{
    CAM_AE_FLICKER_MODE_NOT_SET = -1,
    CAM_AE_FLICKER_MODE_OFF,
    CAM_AE_FLICKER_MODE_50HZ,
    CAM_AE_FLICKER_MODE_60HZ,
    CAM_AE_FLICKER_MODE_AUTO
};
  class AtomAAA;
};

#include <utils/Errors.h>
#include <utils/threads.h>
#include <time.h>
#include "AtomCommon.h"
#include "PlatformData.h"
#include "ia_face.h"
#include "AtomISP.h"
#include "I3AControls.h"
#include <ia_3a_types.h>
#include <ia_types.h>
#include <ia_aiq_types.h>

namespace android {

enum AfMode
{
    CAM_AF_MODE_NOT_SET = -1,
    CAM_AF_MODE_AUTO,
    CAM_AF_MODE_MACRO,
    CAM_AF_MODE_INFINITY,
    CAM_AF_MODE_FIXED,
    CAM_AF_MODE_TOUCH,
    CAM_AF_MODE_MANUAL,
    CAM_AF_MODE_FACE,
    CAM_AF_MODE_CONTINUOUS
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

enum AeMode
{
    CAM_AE_MODE_NOT_SET = -1,
    CAM_AE_MODE_AUTO,
    CAM_AE_MODE_MANUAL,
    CAM_AE_MODE_SHUTTER_PRIORITY,
    CAM_AE_MODE_APERTURE_PRIORITY
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

struct SensorAeConfig
{
    float evBias;
    int expTime;
    short unsigned int aperture_num;
    short unsigned int aperture_denum;
    int aecApexTv;
    int aecApexSv;
    int aecApexAv;
    float digitalGain;
};

struct AAAStatistics
{
    // AE related statistics
    float bv;
    float tv;
    float av;
    float sv;
    // AF related statistics
    int focus_pos;
    // AWB related statistics
    float wb_gain_r;
    float wb_gain_g;
    float wb_gain_b;
};

struct AAALibState
{
    void                           *sh3a_params;
    ia_3a_private_data              sensor_data;
    ia_3a_private_data              motor_data;
    struct atomisp_sensor_mode_data sensor_mode_data;
    bool                            fpn_table_loaded;
    bool                            gdc_table_loaded;
    struct atomisp_3a_statistics   *stats;
    bool                            stats_valid;
    ia_3a_results                   results;
    int                             boot_events;
};

/**
 * \class AtomAAA
 *
 * AtomAAA is a singleton interface to Intel Advanced Camera Imaging
 * Library (formerly known as libmfldadvci).
 *
 * While AAA is the main big module offered by the imaging library,
 * it also provides other functionality. Due to this, in addition
 * to AAAThread that handles actual AAA processing, many other
 * subcomponents of HAL need to use AtomAAA.
 *
 * Due to the non-reentrant design, it is critical that all access
 * to the imaging library go via AtomAAA. To encapsulate the
 * interface, care should be also taken that data types and other
 * definitions in the imaging library are not directly used outside
 * AtomAAA implementation.
 *
 * TODO: The imaging library is being refactorer and will in the
 *       longer term offer object-based interface to different
 *       imaging modules. Once this is complete, the singleton model
 *       will no longer be needed.
 */
class AtomAAA : public I3AControls {

// constructor/destructor
private:
    static AtomAAA* mInstance;
    AtomAAA();
    int setFpnTable(const ia_frame *fpn_table);

    // Common functions for 3A, GBCE, AF etc.
    int applyResults();
    bool reconfigureGrid(void);
    int getStatistics(void);

    // GBCE
    int setGammaEffect(bool inv_gamma);
    int enableGbce(bool enable);

    // 3A control
    int ciAdvInit(const SensorParams *paramFiles, const char *sensorOtpFile);
    void ciAdvUninit(void);
    void ciAdvConfigure(ia_3a_isp_mode mode, float frame_rate);
    void *open3aParamFile(const char *modulename);
    int ciAdvProcessFrame(bool read_stats, const struct timeval *frame_timestamp);
    int processForFlash(ia_3a_flash_stage stage);
    void get3aGridInfo(struct atomisp_grid_info *pgrid);
    void get3aStat(AAAStatistics *pstat);

    // AF
    int getAfScore(bool average_enabled);

    //ISP parameters
    int enableGdc(bool enable);
    int enableFpn(bool enable);
    int enableEe(bool enable);
    int enableNr(bool enable);
    int enableDp(bool enable);
    int enableOb(bool enable);
    int enableShadingCorrection(bool enable);

    // Get exposure time for AE
    void getAeExpCfg(int *exp_time,
                     short unsigned int *aperture_num,
                     short unsigned int *aperture_denum,
                     int *aec_apex_Tv, int *aec_apex_Sv, int *aec_apex_Av,
                     float *digital_gain);
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
    status_t init(const SensorParams *param_files, AtomISP *isp, const char *otpInjectFile = NULL);
    status_t unInit();
    status_t applyIspSettings();
    status_t switchModeAndRate(AtomMode mode, float fps);

    // Getters and Setters
    status_t setAeWindow(const CameraWindow *window);
    status_t setAfWindow(const CameraWindow *window);
    status_t setAeFlickerMode(FlickerMode mode);
    status_t setAfEnabled(bool en);
    status_t setAeMode(AeMode mode);
    AeMode getAeMode();
    status_t setAfMode(AfMode mode);
    AfMode getAfMode();
    void setPublicAeMode(AeMode mode);
    AeMode getPublicAeMode();
    void setPublicAfMode(AfMode mode);
    AfMode getPublicAfMode();
    bool getAfNeedAssistLight();
    status_t setAeFlashMode(FlashMode mode);
    FlashMode getAeFlashMode();
    bool getAeFlashNecessary();
    status_t setAeBacklightCorrection(bool en);
    status_t setTNR(bool en);
    status_t setAeLock(bool en);
    bool     getAeLock();
    status_t setAfLock(bool en);
    bool     getAfLock();
    ia_3a_af_status getCAFStatus();
    status_t setAwbLock(bool en);
    bool     getAwbLock();
    status_t setAwbMapping(ia_3a_awb_map mode);
    ia_3a_awb_map getAwbMapping();
    size_t   getAeMaxNumWindows();
    size_t   getAfMaxNumWindows();
    status_t setAfWindows(const CameraWindow *windows, size_t numWindows);
    status_t setNegativeEffect(bool en);
    status_t getExposureInfo(SensorAeConfig& sensorAeConfig);
    status_t getAeManualBrightness(float *ret);
    status_t setManualFocus(int focus, bool applyNow);
    status_t setManualFocusIncrement(int step);
    status_t updateManualFocus();
    status_t getAfLensPosRange(ia_3a_af_lens_range *lens_range);
    status_t getNextFocusPosition(int *pos);
    status_t getCurrentFocusPosition(int *pos);
    status_t applyEv(float bias);
    status_t setGDC(bool en);
    status_t setManualShutter(float expTime);
    status_t getManualShutter(float *expTime);
    status_t setSmartSceneDetection(bool en);
    bool     getSmartSceneDetection();
    status_t getSmartSceneMode(int *sceneMode, bool *sceneHdr);
    status_t setFaces(const ia_face_state& faceState);

    status_t getGridWindow(AAAWindowInfo& window);

    // I3AControl functions
    virtual void getDefaultParams(CameraParameters *params, CameraParameters *intel_params);
    virtual status_t setEv(float bias);
    virtual status_t getEv(float *ret);
    virtual status_t setAeSceneMode(SceneMode mode);
    virtual SceneMode getAeSceneMode();
    virtual status_t setAwbMode(AwbMode mode);
    virtual AwbMode getAwbMode();
    virtual status_t setManualIso(int iso);
    virtual status_t getManualIso(int *ret);
    virtual status_t setAeMeteringMode(MeteringMode mode);
    virtual MeteringMode getAeMeteringMode();
    virtual status_t set3AColorEffect(v4l2_colorfx effect);

    // ISP processing functions
    status_t apply3AProcess(bool read_stats,
        struct timeval capture_timestamp);

    status_t startStillAf();
    status_t stopStillAf();
    ia_3a_af_status isStillAfComplete();
    status_t applyPreFlashProcess(FlashStage stage);

    // Makernote
    ia_3a_mknote *get3aMakerNote(ia_3a_mknote_mode mode);
    void put3aMakerNote(ia_3a_mknote *mknData);
    void reset3aMakerNote(void);
    int add3aMakerNoteRecord(ia_3a_mknote_field_type mkn_format_id,
                             ia_3a_mknote_field_name mkn_name_id,
                             const void *record,
                             unsigned short record_size);

    //dump 3A statistics
    int dumpCurrent3aStatToFile(void);
    int init3aStatDump(const char * str_mode);
    int deinit3aStatDump(void);
// private members
private:

    struct IspSettings mIspSettings;   // ISP related settings
    Mutex m3aLock;
    bool mHas3A;
    SensorType mSensorType;
    AfMode mAfMode;
    AeMode mPublicAeMode;
    AfMode mPublicAfMode;
    FlashMode mFlashMode;
    AwbMode mAwbMode;
    int mFocusPosition;
    nsecs_t mStillAfStart;
    FILE *pFile3aStatDump;
    AtomISP *mISP;
    ia_env mPrintFunctions;
    AAALibState m3ALibState;
}; // class AtomAAA

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_AAA
