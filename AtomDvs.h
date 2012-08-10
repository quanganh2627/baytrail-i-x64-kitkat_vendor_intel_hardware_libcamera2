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

#ifndef ANDROID_LIBCAMERA_ATOM_DVS
#define ANDROID_LIBCAMERA_ATOM_DVS

#include <utils/Errors.h>
#include "AtomISP.h"

extern "C" {
#include <stdlib.h>
#include <linux/atomisp.h>
}

namespace android {

class AtomDvs {

public:
    AtomDvs(AtomISP *isp);
    ~AtomDvs();
    status_t run();
    status_t reconfigure();

private:
    AtomISP *mIsp;
    Mutex mLock;
    struct atomisp_dis_statistics *mStatistics;
};

};

#endif /* ANDROID_LIBCAMERA_ATOM_DVS */
