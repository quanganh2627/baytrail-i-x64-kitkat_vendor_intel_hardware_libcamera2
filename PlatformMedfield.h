/*
 * Copyright (C) 2012 The Android Open Source Project
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

#include "PlatformData.h"
#include <camera.h>
#include <cutils/properties.h>

/**
 * \file PlatformMedfield.h
 *
 * Platform data for Intel Medfield based products
 */

namespace android {

/**
 * Platform data for Blackbay/MFLD_PR (Medfield based)
 */
class PlatformBlackbay : public PlatformBase {

public:
    PlatformBlackbay(void) {
        mBackRotation = 90;
        mFrontRotation = 90;
        mBackFlash = true;
        mFileInject = true;
        mVideoPreviewSizePref = "1024x580";
    }
};

/**
 * Platform data for Lexington/MFLD_GI (Medfield based)
 */
class PlatformLexington : public PlatformBlackbay {

public:
    PlatformLexington(void) {
        mBackFlash = false;
    }
};

/**
 * Platform data for Redridge/MFLD_DV (Medfield based)
 */
class PlatformRedridge : public PlatformBlackbay {

public:
    PlatformRedridge(void) {

        char bid[PROPERTY_VALUE_MAX] = "";
        property_get("ro.board.id", bid, "");

        if (!strcmp(bid, "redridge_dv10") || !strcmp(bid,"joki_ev20")) {
            mBackRotation = 180;
            mFrontRotation = 0;
        }
        else if (!strcmp(bid,"redridge_dv20") || !strcmp(bid,"redridge_dv21")) {
            mBackRotation = 0;
            mFrontRotation = 0;
        }
        mVideoPreviewSizePref = "720x576";
    }
};


}; // namespace android
