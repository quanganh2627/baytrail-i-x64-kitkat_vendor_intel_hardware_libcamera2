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

#ifndef ANDROID_LIBCAMERA_AIQ_AAA
#define ANDROID_LIBCAMERA_AIQ_AAA


namespace android {
class AtomAIQ;
};

#include <utils/Errors.h>
#include <utils/threads.h>
#include <time.h>
#include <ia_3a_types.h>
#include <ia_types.h>
#include <ia_aiq_types.h>
#include <ia_aiq.h>
#include "AtomCommon.h"
#include "AtomISP.h"
#include "I3AControls.h"
#include "PlatformData.h"

#include "ia_face.h"

namespace android {
// DetermineFlash: returns true if flash should be determined according to current exposure
#define DetermineFlash(x) (x == CAM_AE_FLASH_MODE_AUTO || \
                           x == CAM_AE_FLASH_MODE_DAY_SYNC || \
                           x == CAM_AE_FLASH_MODE_SLOW_SYNC) \



#define DEFAULT_GBCE            true
#define DEFAULT_GBCE_STRENGTH   0
#define AIQ_MAX_TIME_FOR_AF     2500 // milliseconds
#define TORCH_INTENSITY         20   // 20%
#define EV_LOWER_BOUND         -100
#define EV_UPPER_BOUND          100
#define MAX_NUM_AF_WINDOW       9

enum MainFlashStage
{
    CAM_FLASH_STAGE_AF= -1 ,
    CAM_FLASH_STAGE_AE,
    CAM_FLASH_STAGE_FIN,
};

typedef struct {
    struct atomisp_parm               isp_params;
    void*                             aic_output;
    bool                              exposure_changed;
    bool                              flash_intensity_changed;
} aiq_results;

typedef struct {
    ia_aiq_af_results              *af_results;
    ia_aiq_rect                     focus_rect;
    ia_aiq_manual_focus_parameters  focus_parameters;
    struct timespec                 lens_timestamp;
    int32_t                         lens_position;
    bool                            aec_locked;
    bool                            af_locked;
    AfMode                          afMode;
    int                             af_score_window_size;
} af_state;

typedef struct {
    bool                              result_changed;
    bool                              flash_result_changed;
    bool                              force_update;
    bool                              ae_locked;
    struct atomisp_exposure           exposure; //ToDo: remove
    ia_aiq_flash_parameters           flash_parameter;
    ia_aiq_exposure_sensor_descriptor sensor_descriptor;
    ia_aiq_ae_results                *ae_results;
    ia_aiq_ae_results                 prev_results;
    ia_aiq_exposure_parameters        prev_results_exposure;
    ia_aiq_exposure_parameters        prev_exposure[3];
} ae_state;

typedef struct {
    struct atomisp_grid_info        curr_grid_info;
    bool                            reconfigured;
    ia_face_state                  *faces;
    ia_aiq                         *ia_aiq_handle;
    ia_aiq_scene_mode               detected_scene;
    ia_aiq_rgbs_grid                rgbs_grid;
    ia_aiq_af_grid                  af_grid;
    ia_aiq_sensor_frame_params      sensor_frame_params;
    ia_aiq_awb_manual_cct_range     cct_range;
    bool                            dsd_enabled;
    bool                            aic_enabled;
    ia_aiq_frame_use                frame_use;
    ia_aiq_statistics_input_params  statistics_input_parameters;
    ia_aiq_dsd_input_params         dsd_input_parameters;
    struct atomisp_3a_statistics   *stats;
    bool                            stats_valid;
    int                             boot_events;
    struct timespec                 lens_timestamp;
    aiq_results                     results;
} aaa_state;

/**
 * \class AtomAIQ
 *
 * AtomAIQ is a singleton interface to Intel 3A Library (formerly known as libia_aiq).
 *
 * The libia_aiq library provides the 3A functionality(AF, AEC, AWB, GBCE, DSD, AIC)
 * Due to this, in addition to AAAThread that handles actual AAA processing, many other
 * subcomponents of HAL need to use AtomAIQ.
 *
 * All access to the imaging library go via AtomAIQ.
 *
 */
class AtomAIQ : public I3AControls {

// constructor/destructor
private:
    static AtomAIQ* mInstance;
    AtomAIQ(AtomISP *anISP);


    int setFpnTable(const ia_frame *fpn_table);
    status_t getAiqConfig(ia_binary_data *cpfData);

    // Common functions for 3A, GBCE, AF etc.
    status_t run3aMain(const struct timeval *frame_timestamp,
                             struct timeval *sof_timestamp,bool afRun);
    //AE for flash
    int run3aMain();
    int AeForFlash();
    int applyResults();
    bool changeSensorMode(void);

    //staticstics
    int getStatistics(void);
    struct atomisp_3a_statistics * allocateStatistics(int grid_size);
    void freeStatistics(struct atomisp_3a_statistics *stats);
    bool needStatistics();

    // GBCE
    int setGammaEffect(bool inv_gamma);
    int enableGbce(bool enable);
    void resetGBCEParams();
    void runGBCEMain();

    // 3A control
    int run3aInit();
    int processForFlash();
    void get3aGridInfo(struct atomisp_grid_info *pgrid);
    void get3aStat();
    status_t populateFrameInfo(const struct timeval *frame_timestamp,
                                     struct timeval *sof_timestamp);

    //AIC
    void runAICMain();

    // AF
    void resetAFParams();
    void runAfMain();
    void setAfFocusMode(ia_aiq_af_operation_mode mode);
    void setAfFocusRange(ia_aiq_af_range range);
    void setAfMeteringMode(ia_aiq_af_metering_mode mode);
    status_t moveFocusDriveToPos(long position);
    void afUpdateTimestamp(void);


    //AE
    void resetAECParams();
    void runAeMain();
    bool getAeResults();
    bool getAeFlashResults();

    //AWB
    void resetAWBParams();
    void runAwbMain();
    bool getAwbResults();

    //DSD
    void resetDSDParams();
    void runDSDMain();

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

    void getSensorFrameParams(ia_aiq_sensor_frame_params *frame_params);
    int dumpMknToFile();

public:
    static AtomAIQ* getInstance(AtomISP *anISP = NULL) {
        if (mInstance == NULL) {
            if (anISP == NULL) {
                LOGE("trying to get Intel 3A class before initializing it ");
                return NULL;
            }
            mInstance = new AtomAIQ(anISP);
        }
        return mInstance;
    }
    ~AtomAIQ();

    virtual bool isIntel3A() { return true; }
    void getDefaultParams(CameraParameters *params, CameraParameters *intel_params);

    // Initialization functions
    virtual status_t init3A();
    virtual status_t deinit3A();
    virtual status_t switchModeAndRate(AtomMode mode, float fps);

    // Getters and Setters
    status_t setAeWindow(const CameraWindow *window);
    status_t setAfWindow(const CameraWindow *window);
    status_t setAeFlickerMode(FlickerMode mode);
    status_t setAfEnabled(bool en) { return 0; }
    status_t setAeSceneMode(SceneMode mode);
    SceneMode getAeSceneMode();
    status_t setAeMode(AeMode mode);
    AeMode getAeMode();
    status_t setAfMode(AfMode mode);
    AfMode getAfMode();
    bool getAfNeedAssistLight();
    status_t setAeFlashMode(FlashMode mode);
    FlashMode getAeFlashMode();
    bool getAeFlashNecessary();
    status_t setAwbMode(AwbMode mode);
    AwbMode getAwbMode();
    ia_3a_awb_light_source getLightSource(){ return ia_3a_awb_light_source_other; };
    status_t setAeMeteringMode(MeteringMode mode);
    MeteringMode getAeMeteringMode();
    status_t setAeBacklightCorrection(bool en) { return INVALID_OPERATION; }
    status_t set3AColorEffect(const char *effect);
    virtual void setPublicAeMode(AeMode mode);
    virtual AeMode getPublicAeMode();
    virtual void setPublicAfMode(AfMode mode);
    virtual AfMode getPublicAfMode();
    virtual status_t setIsoMode(IsoMode mode){return NO_ERROR;};
    virtual IsoMode getIsoMode(void) {return CAM_AE_ISO_MODE_NOT_SET;};

    status_t setAeLock(bool en);
    bool     getAeLock();
    status_t setAfLock(bool en);
    bool     getAfLock();
    ia_3a_af_status getCAFStatus();
    status_t setAwbLock(bool en);
    bool     getAwbLock();
    //Keep backwards compability with Acute Logic 3A
    status_t setAwbMapping(ia_3a_awb_map mode) { return 0; }
    ia_3a_awb_map getAwbMapping();
    // returning an error in the following functions will cause some functions
    // not to be run in ControlThread
    size_t   getAeMaxNumWindows() { return 0; }
    size_t   getAfMaxNumWindows() { return MAX_NUM_AF_WINDOW; }
    status_t setAfWindows(const CameraWindow *windows, size_t numWindows);
    status_t getExposureInfo(SensorAeConfig& sensorAeConfig);
    status_t getAeManualBrightness(float *ret);
    status_t setManualFocus(int focus, bool applyNow);
    status_t setManualFocusIncrement(int step) { return INVALID_OPERATION; }
    status_t updateManualFocus() { return INVALID_OPERATION; }
    status_t getAfLensPosRange(ia_3a_af_lens_range *lens_range) { return INVALID_OPERATION; }
    status_t getNextFocusPosition(int *pos) { return INVALID_OPERATION; }
    status_t getCurrentFocusPosition(int *pos) { return INVALID_OPERATION; }
    status_t applyEv(float bias);
    status_t setEv(float bias);
    status_t getEv(float *ret);

    status_t setManualIso(int ret);
    status_t getManualIso(int *ret);
    status_t setManualShutter(float expTime);
    status_t getManualShutter(float *expTime);
    status_t setSmartSceneDetection(bool en);
    bool     getSmartSceneDetection();
    status_t getSmartSceneMode(int *sceneMode, bool *sceneHdr);
    status_t setFaces(const ia_face_state& faceState);

    status_t getGridWindow(AAAWindowInfo& window);

    //Bracketing
    status_t initAfBracketing(int stop, ia_aiq_af_bracketing_mode mode = ia_aiq_af_bracketing_mode_symmetric);

    // Flash control
    virtual status_t setFlash(int numFrames);

    // ISP processing functions
    status_t apply3AProcess(bool read_stats,
        struct timeval capture_timestamp,
        struct timeval sof_timestamp);

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
    int dumpCurrent3aStatToFile(void) { return INVALID_OPERATION; }
    int init3aStatDump(const char * str_mode) { return INVALID_OPERATION; }
    int deinit3aStatDump(void) { return INVALID_OPERATION; }


        // TODO: no support, should be removed
    status_t setGDC(bool en){return false;}
    status_t setTNR(bool en) {return false;}


    // Not supported by Intel 3A
    virtual status_t setSaturation(int saturation) { return INVALID_OPERATION; }
    virtual status_t setContrast(int contrast) { return INVALID_OPERATION; }
    virtual status_t setSharpness(int sharpness) { return INVALID_OPERATION; }

// private members
private:

    FILE *pFile3aStatDump;
    AtomISP *mISP;
    ia_env mPrintFunctions;

    aaa_state m3aState;

    //STASTICS
    ia_aiq_statistics_input_params mStatisticsInputParameters;

    //AF
    AfMode mAfMode;
    nsecs_t mStillAfStart;
    ia_aiq_af_input_params mAfInputParameters;
    af_state mAfState;
    MainFlashStage mFlashStage;
    int mFocusPosition;

    //AF bracketing
    ia_aiq_af_bracketing_results* mAfBracketingResult;

    //FLASH
    FlashMode mFlashMode;

    //AE
    ia_aiq_ae_input_params mAeInputParameters;
    AeMode mAeMode;
    SceneMode mAeSceneMode;
    FlashMode mAeFlashMode;
    ae_state mAeState;

    //AW
    ia_aiq_awb_input_params mAwbInputParameters;
    ia_aiq_awb_results *mAwbResults;
    AwbMode mAwbMode;
    bool mAwbLocked;
    int mAwbRunCount;

    //GBCE
    ia_aiq_gbce_results *mGBCEResults;
    bool mGBCEEnable;


    //AIC
    ia_aiq_aic_input_params mAICInputParameters;

    //DSD
    ia_aiq_dsd_input_params mDSDInputParameters;
    ia_aiq_scene_mode mDetectedSceneMode;

    //MKN
    ia_mkn  *mMkn;

}; // class AtomAIQ

}; // namespace android

#endif // ANDROID_LIBCAMERA_AIQ_AAA

