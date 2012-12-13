/*
 * Copyright (c) 2012 Intel Corporation
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

#ifndef I3ACONTROLS_H_
#define I3ACONTROLS_H_

#include <camera/CameraParameters.h>

namespace android {

enum SceneMode
{
    CAM_AE_SCENE_MODE_NOT_SET = -1,
    CAM_AE_SCENE_MODE_AUTO,
    CAM_AE_SCENE_MODE_PORTRAIT,
    CAM_AE_SCENE_MODE_SPORTS,
    CAM_AE_SCENE_MODE_LANDSCAPE,
    CAM_AE_SCENE_MODE_NIGHT,
    CAM_AE_SCENE_MODE_NIGHT_PORTRAIT,
    CAM_AE_SCENE_MODE_FIREWORKS,
    CAM_AE_SCENE_MODE_TEXT,
    CAM_AE_SCENE_MODE_SUNSET,
    CAM_AE_SCENE_MODE_PARTY,
    CAM_AE_SCENE_MODE_CANDLELIGHT
};

enum AwbMode
{
    CAM_AWB_MODE_NOT_SET = -1,
    CAM_AWB_MODE_AUTO,
    CAM_AWB_MODE_MANUAL_INPUT,
    CAM_AWB_MODE_DAYLIGHT,
    CAM_AWB_MODE_SUNSET,
    CAM_AWB_MODE_CLOUDY,
    CAM_AWB_MODE_TUNGSTEN,
    CAM_AWB_MODE_FLUORESCENT,
    CAM_AWB_MODE_WARM_FLUORESCENT,
    CAM_AWB_MODE_SHADOW,
    CAM_AWB_MODE_WARM_INCANDESCENT
};

enum MeteringMode
{
    CAM_AE_METERING_MODE_NOT_SET = -1,
    CAM_AE_METERING_MODE_AUTO,
    CAM_AE_METERING_MODE_SPOT,
    CAM_AE_METERING_MODE_CENTER,
    CAM_AE_METERING_MODE_CUSTOMIZED
};

/** add Iso mode*/
/* ISO control mode setting */
enum IsoMode {
    CAM_AE_ISO_MODE_NOT_SET = -1,
    CAM_AE_ISO_MODE_AUTO,   /* Automatic */
    CAM_AE_ISO_MODE_MANUAL  /* Manual */
};

/**
 * I3AControls defines an interface for 3A controls.
 * For RAW cameras the 3A controls are handled in Intel 3A library,
 * and for SoC cameras they are set via V4L2 commands and handled
 * in the driver.
 *
 * This interface is implemented by AtomAAA (Intel 3A) and
 * AtomISP (V4L2 3A) classes.
 */
class I3AControls
{
public:
    virtual ~I3AControls() {}

    virtual void getDefaultParams(CameraParameters *params, CameraParameters *intel_params) = 0;
    virtual status_t setEv(float bias) = 0;
    virtual status_t getEv(float *ret) = 0;
    virtual status_t setAeSceneMode(SceneMode mode) = 0;
    virtual SceneMode getAeSceneMode() = 0;
    virtual status_t setAwbMode(AwbMode mode) = 0;
    virtual AwbMode getAwbMode() = 0;
    virtual status_t setManualIso(int iso) = 0;
    virtual status_t getManualIso(int *ret) = 0;
    /** expose iso mode setting*/
    virtual status_t setIsoMode(IsoMode mode) = 0;
    virtual IsoMode getIsoMode(void) = 0;
    virtual status_t setAeMeteringMode(MeteringMode mode) = 0;
    virtual MeteringMode getAeMeteringMode() = 0;
    virtual status_t set3AColorEffect(v4l2_colorfx effect) = 0;
};

}

#endif /* I3ACONTROLS_H_ */
