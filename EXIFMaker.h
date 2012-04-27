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

#include "JpegEncoder.h"
#include "AtomAAA.h"
#include <camera/CameraParameters.h>

#ifndef EXIFMAKER_H_
#define EXIFMAKER_H_

namespace android {

#define MAX_EXIF_SIZE 0xFFFF

class EXIFMaker {
private:
    JpegEncoder encoder;
    AtomAAA *mAAA;
    SensorParams sensorParams;
    exif_attribute_t exifAttributes;
    int thumbWidth;
    int thumbHeight;
    size_t exifSize;
    bool initialized;

    void initializeLocation(const CameraParameters &params);
    void initializeHWSpecific(const atomisp_makernote_info &makerNote);
    void clear();
public:
    EXIFMaker();
    ~EXIFMaker();

    void initialize(const CameraParameters &params, const atomisp_makernote_info &makerNote);
    bool isInitialized() { return initialized; }
    void enableFlash();
    void setThumbnail(unsigned char *data, size_t size);
    size_t makeExif(unsigned char **data);
    const SensorParams& getSensorParams() { return sensorParams; }
    void setSensorParams(const SensorParams& sensorParams);
};

}; // namespace android

#endif /* EXIFMAKER_H_ */
