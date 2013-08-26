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
#define LOG_TAG "Camera_DVS2"

#include "PlatformData.h"
#include "AtomDvs2.h"

namespace android {

#define DIGITAL_ZOOM_RATIO 1.0
#define DVS_MIN_ENVELOPE 6
#define NUMS_DVS2_STATS_BUF 8

static ia_dvs2_axis_weight axis_weight = {80, 15, 5, 0, 0};
static ia_dvs2_distortion_coefs dvs2_distortion_coefs = {
    0.f, 0.f, 0.f, 0.f, 0.f};

AtomDvs2::AtomDvs2(HWControlGroup &hwcg) :
    IDvs(hwcg)
    ,mStatistics(NULL)
    ,mDvs2Config(NULL)
    ,mEnabled(false)
{
    LOG1("@%s", __FUNCTION__);
    m_dvs2_characteristics.num_axis = ia_dvs2_algorihm_6_axis;

    /**< effective vertical scan ratio, used for rolling correction
      (Non-blanking ration of frame interval) */
    m_dvs2_characteristics.nonblanking_ratio = 0.88f;
    m_dvs2_characteristics.min_local_motion = 0.00f;

    for (int i = 0; i < 6; i++) {
        m_dvs2_characteristics.cutoff_frequency[i] = 0.0f;
    }
    init();
}

status_t AtomDvs2::init()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    ia_err err =  dvs_init(&mState, NULL, NULL);
    if (err != ia_err_none) {
        LOGE("Failed to initilize the DVS library");
        status = NO_INIT;
    }else {
        memset(&mGdcConfig, 0, sizeof(mGdcConfig));
    }

    return status;
}

AtomDvs2::~AtomDvs2()
{
    if(mDvs2Config) {
        dvs_free_morph_table(mDvs2Config);
        mDvs2Config = NULL;
    }

    if(mStatistics) {
        dvs_free_statistics(mStatistics);
        mStatistics = NULL;
    }

    dvs_deinit(mState);
}

status_t AtomDvs2::reconfigure()
{
    Mutex::Autolock lock(mLock);
    return reconfigureNoLock();
}

status_t AtomDvs2::allocateDvs2Statistics(atomisp_dvs_grid_info info)
{
    status_t status = NO_ERROR;
    ia_err err;
    if (mStatistics) {
        free(mStatistics);
        mStatistics = NULL;
        m_ndvs2StatSize = 0;
    }

    if (mDvs2Config) {
        dvs_free_morph_table(mDvs2Config);
        mDvs2Config = NULL;
    }

    int elems = info.aligned_width * info.aligned_height;
    int nBufferLen = sizeof(atomisp_dis_statistics) + (NUMS_DVS2_STATS_BUF * elems * sizeof(int32_t));

    err = dvs_allocate_statistics(&info, &mStatistics);
    if (err != ia_err_none) {
        LOG1("dvs_allocate_statistics error:%d", err);
        return UNKNOWN_ERROR;
    }
    if (mStatistics)
        m_ndvs2StatSize = nBufferLen;

    err = dvs_allocate_morph_table(mState, &mDvs2Config);
    if (err != ia_err_none)
        return UNKNOWN_ERROR;

    return status;
}

status_t AtomDvs2::reconfigureNoLock()
{
    status_t status = NO_ERROR;
    ia_dvs2_support_configuration support_config;
    ia_err err = ia_err_none;

    struct atomisp_parm isp_params;
    struct atomisp_dvs_grid_info dvs_grid;

    if (!mState)
        return status;

    status = mIsp->getIspParameters(&isp_params);
    if (status != NO_ERROR)
        return status;

    dvs_grid = isp_params.dvs_grid;

    int width = 0, height = 0;
    mIsp->getVideoSize(&width, &height, NULL);
    int preview_width = 0, preview_height = 0;
    mIsp->getPreviewSize(&preview_width, &preview_height, NULL);
    width = MAX(width, preview_width);
    height = MAX(height, preview_height);

    int bq_frame_width = width/2;
    int bq_frame_height = height/2;
    int dvs_env_width = DVS_MIN_ENVELOPE;
    int dvs_env_height = DVS_MIN_ENVELOPE;

    //Configure DVS
    dvs_env_width = isp_params.dvs_envelop.width;
    dvs_env_height = isp_params.dvs_envelop.height;
    support_config.input_y.width = bq_frame_width + dvs_env_width;
    support_config.input_y.height = bq_frame_height + dvs_env_height;
    support_config.grid_size = dvs_grid.bqs_per_grid_cell;
    support_config.grid_per_area = 1;
    mGdcConfig.source_bq.width_bq = bq_frame_width + dvs_env_width;
    mGdcConfig.source_bq.height_bq = bq_frame_height + dvs_env_height;
    mGdcConfig.output_bq.width_bq = bq_frame_width;
    mGdcConfig.output_bq.height_bq = bq_frame_height; //crop
    mGdcConfig.ispfilter_bq.width_bq = 12/2;
    mGdcConfig.ispfilter_bq.height_bq = 12/2;
    mGdcConfig.envelope_bq.width_bq = dvs_env_width - mGdcConfig.ispfilter_bq.width_bq;
    mGdcConfig.envelope_bq.height_bq = dvs_env_height - mGdcConfig.ispfilter_bq.height_bq;
    mGdcConfig.axis_weight = axis_weight;
    mGdcConfig.oxdim_y = 64;
    mGdcConfig.oydim_y = 64;
    mGdcConfig.oxdim_uv = 64;
    mGdcConfig.oydim_uv = 32;

    mGdcConfig.hw_config.scan_mode = ia_dvs2_gdc_scan_mode_stb; //hardcoded
    mGdcConfig.hw_config.interpolation = ia_dvs2_gdc_interpolation_bli; //hardcoded
    mGdcConfig.hw_config.performance_point = ia_dvs2_gdc_performance_point_1x1; //hardcoded
    memcpy(&mGdcConfig.distortion_coefs, &dvs2_distortion_coefs,
           sizeof(ia_dvs2_distortion_coefs));

    if (!mDvs2Config) {
        err = dvs_config(mState, &support_config, &mGdcConfig,
                   &m_dvs2_characteristics, DIGITAL_ZOOM_RATIO, NULL);
    } else {
        err = dvs_reconfig(mState, &support_config, &mGdcConfig,
                   &m_dvs2_characteristics, DIGITAL_ZOOM_RATIO, NULL);
    }

    if (err != ia_err_none) {
        LOGW("Configure DVS failed %d", err);
        return UNKNOWN_ERROR;
    }
    LOG2("Configure DVS succeed");
    LOG2("mEnabled:%s", mEnabled ? "true": "false");
    dvs_disable_motion_compensation(mState, !mEnabled);

    //Allocate statistics
    status = allocateDvs2Statistics(dvs_grid);
    if(!mDvs2Config || status != NO_ERROR) {
        LOGW("Allocate dvs buffers failed");
        return UNKNOWN_ERROR;
    }

    //Set coefficient
    atomisp_dis_coefficients *dvs_coefs = NULL;
    err = dvs_allocate_coefficients(&dvs_grid, &dvs_coefs);
    if (err != ia_err_none) {
        LOGW("allocate dvs2 coeff failed:%d", err);
        return UNKNOWN_ERROR;
    }

    err = dvs_get_coefficients(mState, dvs_coefs);
    if (err != ia_err_none) {
        LOGW("get dvs2 coeff failed: %d", err);
    }else {
        mIsp->setDvsCoefficients(dvs_coefs);
    }
    if (dvs_coefs)
        dvs_free_coefficients(dvs_coefs);
    return status;
}

status_t AtomDvs2::run()
{
    LOG1("@%s", __FUNCTION__);

    Mutex::Autolock lock(mLock);
    status_t status = NO_ERROR;
    ia_err err = ia_err_none;
    bool try_again = false;

    if (!mStatistics || !mState)
        goto end;

    status = mIsp->getDvsStatistics(mStatistics, &try_again);
    if (status != NO_ERROR) {
        LOGW("%s : Failed to get DVS statistics", __FUNCTION__);
        goto end;
    }
    /* When the driver tells us to try again, it means the grid
       has changed. Because of this, we reconfigure the DVS engine
       which will use the updated grid information. */
    if (try_again) {
        reconfigureNoLock();
        status = mIsp->getDvsStatistics(mStatistics, NULL);
        if (status != NO_ERROR) {
            LOGW("%s : Failed to get DVS statistics (again)", __FUNCTION__);
            goto end;
        }
    }

    err = dvs_set_statistics(mState, mStatistics);
    if (err != ia_err_none)
         LOGW(" dvs_set_statistics failed: %d", err);

    if ((err = dvs_execute(mState)) != ia_err_none) {
        LOG2("DVS2 execution failed: %d", err);
        goto end;
    }

    if(mDvs2Config)
    {
        err = dvs_get_morph_table(mState, mDvs2Config);
        if (err == ia_err_none)
            status = mIsp->setDvsConfig(mDvs2Config);
    }

end:
    return status;
}

bool AtomDvs2::enable(const CameraParameters& params)
{
    LOG1("@%s", __FUNCTION__);
    if (isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED, params) &&
        isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION, params)) {
        mEnabled = true;
    }
    mIsp->setDVS(true);
    return true;
}

/**
 * override for IAtomIspObserver::atomIspNotify()
 *
 * AtomDvs2 gets attached to receive preview stream here.
 */
bool AtomDvs2::atomIspNotify(Message *msg, const ObserverState state)
{
    if (!msg) {
        LOG1("Received observer state change");
        return false;
    }

    AtomBuffer *buff = &msg->data.frameBuffer.buff;
    // We only want to run DVS process for non-corrupt frames:
    if (buff && msg->id == MESSAGE_ID_FRAME && buff->status != FRAME_STATUS_CORRUPTED) {
        // run() uses mLock, so this is thread-safe
        run();
    }

    return false;
}

};
