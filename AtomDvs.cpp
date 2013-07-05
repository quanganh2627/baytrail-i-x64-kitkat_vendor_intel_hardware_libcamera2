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
#define LOG_TAG "Camera_DVS"

#include <ia_dvs.h>
#include "AtomDvs.h"

namespace android {

AtomDvs::AtomDvs(AtomISP *isp) :
    mIsp(isp)
    ,mStatistics(NULL)
{
    mState = ia_dvs_create();
    if (!mState)
        LOGE("Failed to create DVS state, DVS will be disabled\n");
}

AtomDvs::~AtomDvs()
{
    if (mStatistics)
        ia_dvs_free_statistics(mStatistics);
    if (mState)
        ia_dvs_destroy(mState);
}

status_t AtomDvs::reconfigure()
{
    Mutex::Autolock lock(mLock);
    return reconfigureNoLock();
}

status_t AtomDvs::reconfigureNoLock()
{
    status_t status = NO_ERROR;
    struct atomisp_parm isp_params;
    const struct atomisp_dis_coefficients *coefs;

    if (!mState)
        return status;
    status = mIsp->getIspParameters(&isp_params);
    if (status != NO_ERROR)
        return status;

    coefs = ia_dvs_set_grid_info(mState, &isp_params.info);
    if (coefs) {
        status = mIsp->setDvsCoefficients(coefs);
        if (mStatistics)
            ia_dvs_free_statistics(mStatistics);
        mStatistics = ia_dvs_allocate_statistics(mState);
        if(mStatistics == NULL) {
            LOGE("Failed to allocate DVS statistics");
            status = NO_MEMORY;
        }
    }
    return status;
}

status_t AtomDvs::run()
{
    Mutex::Autolock lock(mLock);
    status_t status = NO_ERROR;
    struct atomisp_dis_vector vector;
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

    if (!ia_dvs_process(mState, mStatistics, &vector)) {
        status = UNKNOWN_ERROR;
        LOGE("%s : Failed to process DVS ", __FUNCTION__);
        goto end;
    }

    status = mIsp->setMotionVector(&vector);

end:
    return status;
}

bool AtomDvs::enable(const CameraParameters& params)
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    int width = 0, height = 0;
    bool isDVSActive = false;

    if (isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED, params) &&
        isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION, params)) {
        isDVSActive = true;
    }

    params.getVideoSize(&width, &height);

    if (width < MIN_DVS_WIDTH && height < MIN_DVS_HEIGHT)
        isDVSActive = false;

    status = mIsp->setDVS(isDVSActive);

    if (status != NO_ERROR) {
        LOGW("@%s: Failed to set DVS %s", __FUNCTION__, isDVSActive ? "enabled" : "disabled");
        isDVSActive = false;
    }

    return isDVSActive;
}

/**
 * override for IAtomIspObserver::atomIspNotify()
 *
 * AtomDvs gets attached to receive preview stream here.
 */
bool AtomDvs::atomIspNotify(Message *msg, const ObserverState state)
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
