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

#ifndef ANDROID_LIBCAMERA_ATOM_CP
#define ANDROID_LIBCAMERA_ATOM_CP

#include <utils/Errors.h>
#include <utils/threads.h>
#include "AtomISP.h"
#include <ia_cp_types.h>

extern "C" {
#include <stdlib.h>
#include <linux/atomisp.h>
}

namespace android {

enum HdrSharpening {
    NO_SHARPENING = 0,
    NORMAL_SHARPENING,
    STRONG_SHARPENING
};

enum HdrVividness {
    NO_VIVIDNESS = 0,
    GAUSSIAN_VIVIDNESS,
    GAMMA_VIVIDNESS
};

struct CiUserBuffer {
    ia_frame *ciMainBuf;
    ia_frame *ciPostviewBuf;
    ia_cp_histogram *hist;
    size_t ciBufNum;
};

#ifdef ENABLE_INTEL_EXTRAS
#define STUB
#define STAT_STUB
#else
#define STUB {}
#define STAT_STUB {return INVALID_OPERATION;}
#endif

class AtomCP {

public:
    AtomCP(AtomISP *isp)STUB;
    ~AtomCP()STUB;
    status_t computeCDF(const CiUserBuffer& inputBuf, size_t bufIndex)STAT_STUB;
    status_t composeHDR(const CiUserBuffer& inputBuf, const CiUserBuffer& outputBuf,
                        unsigned vividness, unsigned sharpening)STAT_STUB;
    static status_t setIaFrameFormat(ia_frame *inputBuf, int v4l2Format)STAT_STUB;

private:
    ia_env mPrintFunctions;
    ia_acceleration mAccAPI;
    AtomISP *mISP;
    Mutex mLock;
};

};

#endif /* ANDROID_LIBCAMERA_ATOM_CP */
