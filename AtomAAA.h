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
  class AtomAAA;
};

#include <utils/Errors.h>
#include <utils/threads.h>
#include <time.h>
#include "AtomCommon.h"
#include "PlatformData.h"
#include "ia_face.h"
#include "I3AControls.h"
#include "ICameraHwControls.h"
#include <ia_types.h>
#include <ia_aiq_types.h>

namespace android {

#define DEFAULT_GBCE            true
#define DEFAULT_GBCE_STRENGTH   0
#define MAX_TIME_FOR_AF         2500 // milliseconds
#define TORCH_INTENSITY         20   // 20%

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
    // Common functions for 3A, GBCE, AF etc.
    int applyResults();
    bool reconfigureGrid(void);
    int getStatistics(void);

    // GBCE
    int setGammaEffect(bool inv_gamma);
    int enableGbce(bool enable);

    // 3A control
    status_t _init3A();
    int ciAdvInit(const SensorParams *paramFiles, const char *sensorOtpFile);
    void ciAdvUninit(void);
    void ciAdvConfigure(ia_3a_isp_mode mode, float frame_rate);
    void *open3aParamFile(const char *modulename);
    int ciAdvProcessFrame(bool read_stats,
                          const struct timeval *frame_timestamp,
                          const struct timeval *sof_timestamp);
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
                     float *digital_gain, float *total_gain);

    void getSensorFrameParams(ia_aiq_frame_params *frame_params,
                              struct atomisp_sensor_mode_data *sensor_mode_data);

// prevent copy constructor and assignment operator
private:
    AtomAAA(const AtomAAA& other);
    AtomAAA& operator=(const AtomAAA& other);

public:
    AtomAAA(HWControlGroup &hwcg);
    ~AtomAAA();

    // Initialization functions
    virtual status_t init3A();
    virtual status_t deinit3A();
    virtual status_t switchModeAndRate(AtomMode mode, float fps);

    // Getters and Setters
    status_t setAfWindow(const CameraWindow *window);
    status_t setTNR(bool en);
    ia_3a_awb_map getAwbMapping();
    virtual size_t   getAeMaxNumWindows();
    virtual size_t   getAfMaxNumWindows();
    virtual status_t setAfWindows(const CameraWindow *windows, size_t numWindows);
    virtual status_t getExposureInfo(SensorAeConfig& sensorAeConfig);
    virtual  status_t getAeManualBrightness(float *ret);
    status_t setManualFocus(int focus, bool applyNow);
    status_t getNextFocusPosition(int *pos);
    status_t setGDC(bool en);
    status_t getManualShutter(float *expTime);

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
    /** expose iso mode setting*/
    virtual status_t setIsoMode(IsoMode mode);
    virtual IsoMode getIsoMode(void);
    virtual status_t setAeMeteringMode(MeteringMode mode);
    virtual MeteringMode getAeMeteringMode();
    virtual status_t set3AColorEffect(const char *effect);
    virtual status_t setAeMode(AeMode mode);
    virtual AeMode getAeMode();
    virtual status_t setAfMode(AfMode mode);
    virtual AfMode getAfMode();
    virtual status_t setAfEnabled(bool en);
    virtual status_t setAeWindow(const CameraWindow *window);
    virtual status_t setAeFlickerMode(FlickerMode mode);

    virtual bool isIntel3A() { return true; }
    virtual status_t getAfLensPosRange(ia_3a_af_lens_range *lens_range);
    virtual status_t getCurrentFocusPosition(int *pos);
    virtual status_t setManualFocusIncrement(int step);
    virtual status_t updateManualFocus();
    virtual status_t applyEv(float bias);
    virtual status_t getGridWindow(AAAWindowInfo& window);
    virtual bool     getAeLock();
    virtual status_t setAeLock(bool en);
    virtual bool     getAfLock();
    virtual status_t setAfLock(bool en);
    virtual status_t setAeFlashMode(FlashMode mode);
    virtual status_t setAwbLock(bool en);
    virtual bool     getAwbLock();
    virtual FlashMode getAeFlashMode();
    virtual bool getAfNeedAssistLight();
    virtual bool getAeFlashNecessary();
    virtual ia_3a_awb_light_source getLightSource();
    virtual status_t setManualShutter(float expTime);
    virtual status_t setAwbMapping(ia_3a_awb_map mode);
    virtual status_t setSmartSceneDetection(bool en);
    virtual bool     getSmartSceneDetection();
    virtual status_t getSmartSceneMode(int *sceneMode, bool *sceneHdr);
    virtual void setPublicAeMode(AeMode mode);
    virtual AeMode getPublicAeMode();
    virtual ia_3a_af_status getCAFStatus();
    virtual status_t setFaces(const ia_face_state& faceState);

    // Flash control
    virtual status_t setFlash(int numFrames);

    // ISP processing functions
    status_t apply3AProcess(bool read_stats, struct timeval capture_timestamp,
                                             struct timeval sof_timestamp);

    virtual status_t startStillAf();
    virtual status_t stopStillAf();
    virtual ia_3a_af_status isStillAfComplete();
    virtual status_t applyPreFlashProcess(FlashStage stage);

    // Makernote
    virtual ia_3a_mknote *get3aMakerNote(ia_3a_mknote_mode mode);
    virtual void put3aMakerNote(ia_3a_mknote *mknData);
    virtual void reset3aMakerNote(void);
    virtual int add3aMakerNoteRecord(ia_3a_mknote_field_type mkn_format_id,
                                     ia_3a_mknote_field_name mkn_name_id,
                                     const void *record,
                                     unsigned short record_size);

    //dump 3A statistics
    virtual int dumpCurrent3aStatToFile(void);
    virtual int init3aStatDump(const char * str_mode);
    virtual int deinit3aStatDump(void);

    // Bracketing
    virtual status_t initAfBracketing(int stops,  AFBracketingMode mode) { return INVALID_OPERATION; }
    virtual status_t initAeBracketing() { return INVALID_OPERATION; }

// private members
private:

    Mutex m3aLock;
    SensorType mSensorType;
    AfMode mAfMode;
    AeMode mPublicAeMode;
    FlashMode mFlashMode;
    FlashStage mFlashStage;
    AwbMode mAwbMode;
    ia_3a_awb_light_source m3ALightSource;
    int mFocusPosition;
    nsecs_t mStillAfStart;
    FILE *pFile3aStatDump;
    IHWIspControl *mISP;
    IHWFlashControl *mFlashCI;
    IHWSensorControl *mSensorCI;
    ia_env mPrintFunctions;
    AAALibState m3ALibState;
    uint64_t mTimePreviousFlash;
    uint64_t mTimeAssistRequired;
    static const uint32_t TIME_STICKY_FLASH_USAGE_NS = 1300000000; // 1300ms
    static const uint32_t TIME_ASSIST_DECIDES_FLASH_USAGE_NS = 2000000000; // 2000ms

}; // class AtomAAA

}; // namespace android

#endif // ANDROID_LIBCAMERA_ATOM_AAA
