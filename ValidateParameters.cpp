/*
 * Copyright (c) 2014 Intel Corporation.
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

#define LOG_TAG "Camera_ValidateParameters"

#include "ValidateParameters.h"
#include "AtomCommon.h"
#include "LogHelper.h"

#include <stdlib.h>

namespace android {

static bool validateSize(int width, int height, Vector<Size> &supportedSizes)
{
    if (width < 0 || height < 0)
        return false;

    for (Vector<Size>::iterator it = supportedSizes.begin(); it != supportedSizes.end(); ++it)
        if (width == it->width && height == it->height)
            return true;

    LOGW("WARNING: The Size %dx%d is not fully supported. Some issues might occur!", width, height);
    return true;
}

bool validateString(const char* value,  const char* supportList)
{
    // value should not set if support list is empty
    if (value !=NULL && supportList == NULL) {
        return false;
    }

    if (value == NULL || supportList == NULL) {
        return true;
    }

    size_t len = strlen(value);
    const char* startPtr(supportList);
    const char* endPtr(supportList);
    int bracketLevel(0);

    // divide support list to values and compare those to given values.
    // values are separated with comma in support list, but commas also exist
    // part of values inside bracket.
    while (true) {
        if ( *endPtr == '(') {
            ++bracketLevel;
        } else if (*endPtr == ')') {
            --bracketLevel;
        } else if ( bracketLevel == 0 && ( *endPtr == '\0' || *endPtr == ',')) {
            if (((startPtr + len) == endPtr) &&
                (strncmp(value, startPtr, len) == 0)) {
                return true;
            }

            // bracket can use circle values in supported list
            if (((startPtr + len + 2 ) == endPtr) &&
                ( *startPtr == '(') &&
                (strncmp(value, startPtr + 1, len) == 0)) {
                return true;
            }
            startPtr = endPtr + 1;
        }

        if (*endPtr == '\0') {
            return false;
        }
        ++endPtr;
    }

    return false;
}

status_t validateParameters(const CameraParameters *params)
{
    LOG1("@%s: params = %p", __FUNCTION__, params);

    // PREVIEW
    int width, height;
    Vector<Size> supportedSizes;
    params->getSupportedPreviewSizes(supportedSizes);
    params->getPreviewSize(&width, &height);
    if (!validateSize(width, height, supportedSizes)) {
        LOGE("bad preview size");
        return BAD_VALUE;
    }

    // PREVIEW_FPS_RANGE
    int minFPS, maxFPS;
    params->getPreviewFpsRange(&minFPS, &maxFPS);
    // getPreviewFrameRate() returns -1 fps value if the range-pair string is malformatted
    const char* fpsRange = params->get(CameraParameters::KEY_PREVIEW_FPS_RANGE);
    const char* fpsRanges = params->get(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE);
    if ((fpsRange && fpsRanges && strstr(fpsRanges, fpsRange) == NULL) ||
        minFPS < 0 || maxFPS < 0) {
            LOGE("invalid fps range: %s; supported %s", fpsRange, fpsRanges);
            return BAD_VALUE;
    }

    // VIDEO
    params->getVideoSize(&width, &height);
    supportedSizes.clear();
    params->getSupportedVideoSizes(supportedSizes);
    if (!validateSize(width, height, supportedSizes)) {
        LOGE("bad video size %dx%d", width, height);
        return BAD_VALUE;
    }

    // RECORDING FRAME RATE
    const char* recordingFps = params->get(IntelCameraParameters::KEY_RECORDING_FRAME_RATE);
    const char* supportedRecordingFps = params->get(IntelCameraParameters::KEY_SUPPORTED_RECORDING_FRAME_RATES);
    if (!validateString(recordingFps, supportedRecordingFps)) {
        LOGE("bad recording frame rate: %s, supported: %s", recordingFps, supportedRecordingFps);
        return BAD_VALUE;
    }

    // SNAPSHOT
    params->getPictureSize(&width, &height);
    supportedSizes.clear();
    params->getSupportedPictureSizes(supportedSizes);
    if (!validateSize(width, height, supportedSizes)) {
        LOGE("bad picture size");
        return BAD_VALUE;
    }

    // JPEG QUALITY
    int jpegQuality = params->getInt(CameraParameters::KEY_JPEG_QUALITY);
    if (jpegQuality < 1 || jpegQuality > 100) {
        LOGE("bad jpeg quality: %d", jpegQuality);
        return BAD_VALUE;
    }

    // THUMBNAIL QUALITY
    int thumbQuality = params->getInt(CameraParameters::KEY_JPEG_THUMBNAIL_QUALITY);
    if (thumbQuality < 1 || thumbQuality > 100) {
        LOGE("bad thumbnail quality: %d", thumbQuality);
        return BAD_VALUE;
    }

    // THUMBNAIL SIZE
    int thumbWidth = params->getInt(CameraParameters::KEY_JPEG_THUMBNAIL_WIDTH);
    int thumbHeight = params->getInt(CameraParameters::KEY_JPEG_THUMBNAIL_HEIGHT);
    char* thumbnailSizes = (char*) params->get(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES);
    supportedSizes.clear();
    if (thumbnailSizes != NULL) {
        while (true) {
            int width = (int)strtol(thumbnailSizes, &thumbnailSizes, 10);
            int height = (int)strtol(thumbnailSizes+1, &thumbnailSizes, 10);
            supportedSizes.push(Size(width, height));
            if (*thumbnailSizes == '\0')
                break;
            ++thumbnailSizes;
        }
        if (!validateSize(thumbWidth, thumbHeight, supportedSizes)) {
            LOGE("bad thumbnail size: (%d,%d)", thumbWidth, thumbHeight);
            return BAD_VALUE;
        }
    } else {
        LOGE("bad thumbnail size");
        return BAD_VALUE;
    }
    // PICTURE FORMAT
    const char* picFormat = params->get(CameraParameters::KEY_PICTURE_FORMAT);
    const char* picFormats = params->get(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS);
    if (!validateString(picFormat, picFormats)) {
        LOGE("bad picture fourcc: %s", picFormat);
        return BAD_VALUE;
    }

    // PREVIEW FORMAT
    const char* preFormat = params->get(CameraParameters::KEY_PREVIEW_FORMAT);
    const char* preFormats = params->get(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS);
    if (!validateString(preFormat, preFormats))  {
        LOGE("bad preview fourcc: %s", preFormat);
        return BAD_VALUE;
    }

    // ROTATION, can only be 0 ,90, 180 or 270.
    int rotation = params->getInt(CameraParameters::KEY_ROTATION);
    if (rotation != 0 && rotation != 90 && rotation != 180 && rotation != 270) {
        LOGE("bad rotation value: %d", rotation);
        return BAD_VALUE;
    }


    // WHITE BALANCE
    const char* whiteBalance = params->get(CameraParameters::KEY_WHITE_BALANCE);
    const char* whiteBalances = params->get(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE);
    if (!validateString(whiteBalance, whiteBalances)) {
        LOGE("bad white balance mode: %s", whiteBalance);
        return BAD_VALUE;
    }

    // ZOOM
    int zoom = params->getInt(CameraParameters::KEY_ZOOM);
    int maxZoom = params->getInt(CameraParameters::KEY_MAX_ZOOM);
    if (zoom > maxZoom || zoom < 0) {
        LOGE("bad zoom index: %d", zoom);
        return BAD_VALUE;
    }

    // FLASH
    const char* flashMode = params->get(CameraParameters::KEY_FLASH_MODE);
    const char* flashModes = params->get(CameraParameters::KEY_SUPPORTED_FLASH_MODES);
    if (!validateString(flashMode, flashModes)) {
        LOGE("bad flash mode");
        return BAD_VALUE;
    }

    // SCENE MODE
    const char* sceneMode = params->get(CameraParameters::KEY_SCENE_MODE);
    const char* sceneModes = params->get(CameraParameters::KEY_SUPPORTED_SCENE_MODES);
    if (!validateString(sceneMode, sceneModes)) {
        LOGE("bad scene mode: %s; supported: %s", sceneMode, sceneModes);
        return BAD_VALUE;
    }

    // FOCUS
    const char* focusMode = params->get(CameraParameters::KEY_FOCUS_MODE);
    const char* focusModes = params->get(CameraParameters::KEY_SUPPORTED_FOCUS_MODES);
    if (!validateString(focusMode, focusModes)) {
        LOGE("bad focus mode: %s; supported: %s", focusMode, focusModes);
        return BAD_VALUE;
    }

    // BURST LENGTH
    const char* burstLength = params->get(IntelCameraParameters::KEY_BURST_LENGTH);
    const char* burstLengths = params->get(IntelCameraParameters::KEY_SUPPORTED_BURST_LENGTH);
     int burstMaxLengthNegative = params->getInt(IntelCameraParameters::KEY_MAX_BURST_LENGTH_WITH_NEGATIVE_START_INDEX);
    if (!validateString(burstLength, burstLengths)) {
        LOGE("bad burst length: %s; supported: %s", burstLength, burstLengths);
        return BAD_VALUE;
    }
    const char* burstStart = params->get(IntelCameraParameters::KEY_BURST_START_INDEX);
    if (burstStart) {
        int burstStartInt = atoi(burstStart);
        if (burstStartInt < 0) {
            const char* captureBracket = params->get(IntelCameraParameters::KEY_CAPTURE_BRACKET);
            if (captureBracket && String8(captureBracket) != "none") {
                LOGE("negative start-index and bracketing not supported concurrently");
                return BAD_VALUE;
            }
            int len = burstLength ? atoi(burstLength) : 0;
            if (len > burstMaxLengthNegative) {
                LOGE("negative start-index and burst-length=%d not supported concurrently", len);
                return BAD_VALUE;
            }
        }
    }

    // BURST SPEED
    const char* burstSpeed = params->get(IntelCameraParameters::KEY_BURST_SPEED);
    const char* burstSpeeds = params->get(IntelCameraParameters::KEY_SUPPORTED_BURST_SPEED);
    if (!validateString(burstSpeed, burstSpeeds)) {
        LOGE("bad burst speed: %s; supported: %s", burstSpeed, burstSpeeds);
        return BAD_VALUE;
    }

    // OVERLAY
    const char* overlaySupported = params->get(IntelCameraParameters::KEY_HW_OVERLAY_RENDERING_SUPPORTED);
    const char* overlay = params->get(IntelCameraParameters::KEY_HW_OVERLAY_RENDERING);
        if (!validateString(overlay, overlaySupported)) {
        LOGE("bad overlay rendering mode: %s; supported: %s", overlay, overlaySupported);
        return BAD_VALUE;
    }

    // MISCELLANEOUS
    const char *size = params->get(IntelCameraParameters::KEY_PANORAMA_LIVE_PREVIEW_SIZE);
    const char *livePreviewSizes = IntelCameraParameters::getSupportedPanoramaLivePreviewSizes(*params);
    if (!validateString(size, livePreviewSizes)) {
        LOGE("bad panorama live preview size");
        return BAD_VALUE;
    }

    // ANTI FLICKER
    const char* flickerMode = params->get(CameraParameters::KEY_ANTIBANDING);
    const char* flickerModes = params->get(CameraParameters::KEY_SUPPORTED_ANTIBANDING);
    if (!validateString(flickerMode, flickerModes)) {
        LOGE("bad anti flicker mode");
        return BAD_VALUE;
    }

    // COLOR EFFECT
    const char* colorEffect = params->get(CameraParameters::KEY_EFFECT);
    const char* colorEffects = params->get(CameraParameters::KEY_SUPPORTED_EFFECTS);
    if (!validateString(colorEffect, colorEffects)) {
        LOGE("bad color effect: %s", colorEffect);
        return BAD_VALUE;
    }

    // EXPOSURE COMPENSATION
    int exposure = params->getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
    int minExposure = params->getInt(CameraParameters::KEY_MIN_EXPOSURE_COMPENSATION);
    int maxExposure = params->getInt(CameraParameters::KEY_MAX_EXPOSURE_COMPENSATION);
    if (exposure > maxExposure || exposure < minExposure) {
        LOGE("bad exposure compensation value: %d", exposure);
        return BAD_VALUE;
    }

    //Note: here for Intel expand parameters, add additional validity check
    //for their supported list. when they're null, we return bad value for
    //these intel parameters setting. As "noise reduction and edge enhancement"
    //and "multi access color correction" are not supported yet.

    // NOISE_REDUCTION_AND_EDGE_ENHANCEMENT
    const char* noiseReductionAndEdgeEnhancement = params->get(IntelCameraParameters::KEY_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
    const char* noiseReductionAndEdgeEnhancements = params->get(IntelCameraParameters::KEY_SUPPORTED_NOISE_REDUCTION_AND_EDGE_ENHANCEMENT);
    if (!validateString(noiseReductionAndEdgeEnhancement, noiseReductionAndEdgeEnhancements)) {
        LOGE("bad noise reduction and edge enhancement value : %s", noiseReductionAndEdgeEnhancement);
        return BAD_VALUE;
    }

    // MULTI_ACCESS_COLOR_CORRECTION
    const char* multiAccessColorCorrection = params->get(IntelCameraParameters::KEY_MULTI_ACCESS_COLOR_CORRECTION);
    const char* multiAccessColorCorrections = params->get(IntelCameraParameters::KEY_SUPPORTED_MULTI_ACCESS_COLOR_CORRECTIONS);
    if (!validateString(multiAccessColorCorrection, multiAccessColorCorrections)) {
        LOGE("bad multi access color correction value : %s", multiAccessColorCorrection);
        return BAD_VALUE;
    }

    //DVS
    if(isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION, *params)
       && !isParameterSet(CameraParameters::KEY_VIDEO_STABILIZATION_SUPPORTED, *params)) {
        LOGE("bad value for DVS, DVS not support");
        return BAD_VALUE;
    }

    // CONTRAST
    const char* contrastMode = params->get(IntelCameraParameters::KEY_CONTRAST_MODE);
    const char* contrastModes = params->get(IntelCameraParameters::KEY_SUPPORTED_CONTRAST_MODES);
    if (!validateString(contrastMode, contrastModes)) {
        LOGE("bad contrast mode: %s", contrastMode);
        return BAD_VALUE;
    }

    // SATURATION
    const char* saturationMode = params->get(IntelCameraParameters::KEY_SATURATION_MODE);
    const char* saturationModes = params->get(IntelCameraParameters::KEY_SUPPORTED_SATURATION_MODES);
    if (!validateString(saturationMode, saturationModes)) {
        LOGE("bad saturation mode: %s", saturationMode);
        return BAD_VALUE;
    }

    // SHARPNESS
    const char* sharpnessmode = params->get(IntelCameraParameters::KEY_SHARPNESS_MODE);
    const char* sharpnessmodes = params->get(IntelCameraParameters::KEY_SUPPORTED_SHARPNESS_MODES);
    if (!validateString(sharpnessmode, sharpnessmodes)) {
        LOGE("bad sharpness mode: %s", sharpnessmode);
        return BAD_VALUE;
    }

    return NO_ERROR;
}



}
