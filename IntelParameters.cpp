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

#include "IntelParameters.h"

namespace android {

    // IMPORTANT NOTE:
    //
    // Intel specific parameters should be defined here,
    // so that any version of HAL can be built against
    // non-Intel libcamera_client (e.g. vanilla AOS
    // libcamera_client). This allows in cases where customers
    // are making own modifications and may not have all Intel
    // parameters defined in their version of CameraParameters.cpp.

    const char IntelCameraParameters::KEY_FOCUS_WINDOW[] = "focus-window";
    const char IntelCameraParameters::KEY_RAW_DATA_FORMAT[] = "raw-data-format";
    const char IntelCameraParameters::KEY_SUPPORTED_RAW_DATA_FORMATS[] = "raw-data-format-values";
    const char IntelCameraParameters::KEY_CAPTURE_BRACKET[] = "capture-bracket";
    const char IntelCameraParameters::KEY_SUPPORTED_CAPTURE_BRACKET[] = "capture-bracket-values";
    const char IntelCameraParameters::KEY_HDR_IMAGING[] = "hdr-imaging";
    const char IntelCameraParameters::KEY_SUPPORTED_HDR_IMAGING[] = "hdr-imaging-values";
    const char IntelCameraParameters::KEY_HDR_SHARPENING[] = "hdr-sharpening";
    const char IntelCameraParameters::KEY_SUPPORTED_HDR_SHARPENING[] = "hdr-sharpening-values";
    const char IntelCameraParameters::KEY_HDR_VIVIDNESS[] = "hdr-vividness";
    const char IntelCameraParameters::KEY_SUPPORTED_HDR_VIVIDNESS[] = "hdr-vividness-values";
    const char IntelCameraParameters::KEY_HDR_SAVE_ORIGINAL[] = "hdr-save-original";
    const char IntelCameraParameters::KEY_SUPPORTED_HDR_SAVE_ORIGINAL[] = "hdr-save-original-values";
    const char IntelCameraParameters::KEY_ROTATION_MODE[] = "rotation-mode";
    const char IntelCameraParameters::KEY_SUPPORTED_ROTATION_MODES[] = "rotation-mode-values";
    const char IntelCameraParameters::KEY_FRONT_SENSOR_FLIP[] = "front-sensor-flip";

    // 3A related
    const char IntelCameraParameters::KEY_AE_MODE[] = "ae-mode";
    const char IntelCameraParameters::KEY_SUPPORTED_AE_MODES[] = "ae-mode-values";

    const char IntelCameraParameters::KEY_AE_METERING_MODE[] = "ae-metering-mode";
    const char IntelCameraParameters::KEY_SUPPORTED_AE_METERING_MODES[] = "ae-metering-mode-values";
    const char IntelCameraParameters::KEY_AF_METERING_MODE[] = "af-metering-mode";
    const char IntelCameraParameters::KEY_SUPPORTED_AF_METERING_MODES[] = "af-metering-mode-values";
    const char IntelCameraParameters::KEY_AF_LOCK_MODE[] = "af-lock-mode";
    const char IntelCameraParameters::KEY_SUPPORTED_AF_LOCK_MODES[] = "af-lock-mode-values";
    const char IntelCameraParameters::KEY_BACK_LIGHTING_CORRECTION_MODE[] = "back-lighting-correction-mode";
    const char IntelCameraParameters::KEY_SUPPORTED_BACK_LIGHTING_CORRECTION_MODES[] = "back-lighting-correction-mode-values";
    const char IntelCameraParameters::KEY_AWB_MAPPING_MODE[] = "awb-mapping-mode";
    const char IntelCameraParameters::KEY_SUPPORTED_AWB_MAPPING_MODES[] = "awb-mapping-mode-values";
    const char IntelCameraParameters::KEY_SHUTTER[] = "shutter";
    const char IntelCameraParameters::KEY_SUPPORTED_SHUTTER[] = "shutter-values";
    const char IntelCameraParameters::KEY_APERTURE[] = "aperture";
    const char IntelCameraParameters::KEY_SUPPORTED_APERTURE[] = "aperture-values";
    const char IntelCameraParameters::KEY_ISO[] = "iso";
    const char IntelCameraParameters::KEY_SUPPORTED_ISO[] = "iso-values";
    const char IntelCameraParameters::KEY_COLOR_TEMPERATURE[] = "color-temperature";

    // ISP related
    const char IntelCameraParameters::KEY_XNR[] = "xnr";
    const char IntelCameraParameters::KEY_SUPPORTED_XNR[] = "xnr-values";
    const char IntelCameraParameters::KEY_ANR[] = "anr";
    const char IntelCameraParameters::KEY_SUPPORTED_ANR[] = "anr-values";
    const char IntelCameraParameters::KEY_GDC[] = "gdc";
    const char IntelCameraParameters::KEY_SUPPORTED_GDC[] = "gdc-values";
    const char IntelCameraParameters::KEY_TEMPORAL_NOISE_REDUCTION[] = "temporal-noise-reduction";
    const char IntelCameraParameters::KEY_SUPPORTED_TEMPORAL_NOISE_REDUCTION[] = "temporal-noise-reduction-values";
    const char IntelCameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT[] = "noise-reduction-and-edge-enhancement";
    const char IntelCameraParameters::KEY_SUPPORTED_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT[] = "noise-reduction-and-edge-enhancement-values";
    const char IntelCameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION[] = "multi-access-color-correction";
    const char IntelCameraParameters::KEY_SUPPORTED_MULTI_ACCESS_COLOR_CORRECTIONS[] = "multi-access-color-correction-values";

    const char IntelCameraParameters::EFFECT_STILL_SKY_BLUE[] = "still-sky-blue";
    const char IntelCameraParameters::EFFECT_STILL_GRASS_GREEN[] = "still-grass-green";
    const char IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_LOW[] = "still-skin-whiten-low";
    const char IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_MEDIUM[] = "still-skin-whiten-medium";
    const char IntelCameraParameters::EFFECT_STILL_SKIN_WHITEN_HIGH[] = "still-skin-whiten-high";

    // burst capture
    const char IntelCameraParameters::KEY_SUPPORTED_BURST_LENGTH[] = "burst-length-values";
    const char IntelCameraParameters::KEY_BURST_LENGTH[] = "burst-length";
    const char IntelCameraParameters::KEY_SUPPORTED_BURST_FPS[] = "burst-fps-values";
    const char IntelCameraParameters::KEY_BURST_FPS[] = "burst-fps";

    // for 3A
    // Values for ae mode settings
    const char IntelCameraParameters::AE_MODE_AUTO[] = "auto";
    const char IntelCameraParameters::AE_MODE_MANUAL[] = "manual";
    const char IntelCameraParameters::AE_MODE_SHUTTER_PRIORITY[] = "shutter-priority";
    const char IntelCameraParameters::AE_MODE_APERTURE_PRIORITY[] = "aperture-priority";

    // Values for focus mode settings.
    const char IntelCameraParameters::FOCUS_MODE_MANUAL[] = "manual";
    const char IntelCameraParameters::FOCUS_MODE_TOUCH[] = "touch";

    // Values for flash mode settings.
    const char IntelCameraParameters::FLASH_MODE_DAY_SYNC[] = "day-sync";
    const char IntelCameraParameters::FLASH_MODE_SLOW_SYNC[] = "slow-sync";

    //values for ae metering mode
    const char IntelCameraParameters::AE_METERING_MODE_AUTO[] = "auto";
    const char IntelCameraParameters::AE_METERING_MODE_SPOT[] = "spot";
    const char IntelCameraParameters::AE_METERING_MODE_CENTER[] = "center";
    const char IntelCameraParameters::AE_METERING_MODE_CUSTOMIZED[] = "customized";

    //values for af metering mode
    const char IntelCameraParameters::AF_METERING_MODE_AUTO[] = "auto";
    const char IntelCameraParameters::AF_METERING_MODE_SPOT[] = "spot";

    //values for ae lock mode
    const char IntelCameraParameters::AE_LOCK_LOCK[] = "lock";
    const char IntelCameraParameters::AE_LOCK_UNLOCK[] = "unlock";

    //values for af lock mode
    const char IntelCameraParameters::AF_LOCK_LOCK[] = "lock";
    const char IntelCameraParameters::AF_LOCK_UNLOCK[] = "unlock";

    //values for awb lock mode
    const char IntelCameraParameters::AWB_LOCK_LOCK[] = "lock";
    const char IntelCameraParameters::AWB_LOCK_UNLOCK[] = "unlock";

    //values for back light correction mode
    const char IntelCameraParameters::BACK_LIGHT_CORRECTION_ON[] = "on";
    const char IntelCameraParameters::BACK_LIGHT_COORECTION_OFF[] = "off";

    //values for red eye mode
    const char IntelCameraParameters::RED_EYE_REMOVAL_ON[] = "on";
    const char IntelCameraParameters::RED_EYE_REMOVAL_OFF[] = "off";

    //values for awb mapping
    const char IntelCameraParameters::AWB_MAPPING_AUTO[] = "auto";
    const char IntelCameraParameters::AWB_MAPPING_INDOOR[] = "indoor";
    const char IntelCameraParameters::AWB_MAPPING_OUTDOOR[] = "outdoor";

    const char IntelCameraParameters::EFFECT_VIVID[] = "vivid";

    const char IntelCameraParameters::KEY_FILE_INJECT_FILENAME[] = "file-inject-name";
    const char IntelCameraParameters::KEY_FILE_INJECT_WIDTH[] = "file-inject-width";
    const char IntelCameraParameters::KEY_FILE_INJECT_HEIGHT[] = "file-inject-height";
    const char IntelCameraParameters::KEY_FILE_INJECT_BAYER_ORDER[] = "file-inject-bayer-order";
    const char IntelCameraParameters::KEY_FILE_INJECT_FORMAT[] = "file-inject-format";

}; // ns android
