/*
 * Copyright (c) 2014 Intel Corporation
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

#define LOG_TAG "HAL_VS"

#include "HALVideoStabilization.h"
#include "LogHelper.h"
#include "ImageScaler.h"
#include "assert.h"

namespace android {

void HALVideoStabilization::getEnvelopeSize(int previewWidth, int previewHeight, int &envelopeWidth, int &envelopeHeight)
{
    LOG1("@%s", __FUNCTION__);
    envelopeWidth  = 0;
    envelopeHeight = 0;
    if (previewWidth == 1920 && previewHeight == 1080) {
        // because envelope size configuring doesn't work yet (driver issue) for
        // all resolutions, don't configure it yet for 1080p.
        // TODO: remove this if-section after driver works
        envelopeWidth  = 1920;
        envelopeHeight = 1080;
    } else {
        // this is the default. This should be used for all resolutions, but see above 1080p exception..
        envelopeWidth = (previewWidth * ENVELOPE_MULTIPLIER) / ENVELOPE_DIVIDER;
        envelopeHeight = (previewHeight * ENVELOPE_MULTIPLIER) / ENVELOPE_DIVIDER;
    }
    LOG1("@%s: selected envelope size %dx%d for preview %dx%d", __FUNCTION__,
         envelopeWidth, envelopeHeight, previewWidth, previewHeight);
}

void HALVideoStabilization::process(const AtomBuffer *inBuf, AtomBuffer *outBuf)
{
    LOG2("@%s", __FUNCTION__);
    // this function is just an example, this should be replaced with a call to
    // the real VS library
    assert(inBuf && outBuf && inBuf->width >= outBuf->width);

    if (inBuf->width  != outBuf->width) {
        ImageScaler::centerCropNV12orNV21Image(inBuf, outBuf);
    } else {
        // default (1080p, which can't be configured for bigger, envelope size yet)
        memcpy((char *)outBuf->dataPtr, (const char*)inBuf->dataPtr, outBuf->size);
    }
}

}

