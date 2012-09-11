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

AtomDvs::AtomDvs(AtomISP *isp)
{
    mIsp = isp;
    mStatistics = NULL;
    ia_dvs_init();
}

AtomDvs::~AtomDvs()
{
    if (mStatistics)
        ia_dvs_free_statistics(mStatistics);
    ia_dvs_uninit();
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

    status = mIsp->getIspParameters(&isp_params);
    if (status != NO_ERROR)
        return status;

    coefs = ia_dvs_set_grid_info(&isp_params.info);
    if (coefs) {
        status = mIsp->setDvsCoefficients(coefs);
        if (mStatistics)
            ia_dvs_free_statistics(mStatistics);
        mStatistics = ia_dvs_allocate_statistics();
    }
    return status;
}

status_t AtomDvs::run()
{
    Mutex::Autolock lock(mLock);
    status_t status = NO_ERROR;
    struct atomisp_dis_vector vector;
    bool try_again = false;

    if (!mStatistics)
        goto end;

    status = mIsp->getDvsStatistics(mStatistics, &try_again);
    if (status != NO_ERROR)
        goto end;

    /* When the driver tells us to try again, it means the grid
       has changed. Because of this, we reconfigure the DVS engine
       which will use the updated grid information. */
    if (try_again) {
        reconfigureNoLock();
        status = mIsp->getDvsStatistics(mStatistics, NULL);
        if (status != NO_ERROR)
            goto end;
    }

    if (!ia_dvs_process(mStatistics, &vector)) {
        status = UNKNOWN_ERROR;
        goto end;
    }

    status = mIsp->setMotionVector(&vector);

end:
    return status;
}

};
