/*
 * Copyright (C) 2012 The Android Open Source Project
 * Copyright (c) 2013 Intel Corporation. All Rights Reserved.
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
#define LOG_TAG "Camera_Profiles"

#include "LogHelper.h"
#include <string.h>
#include <libexpat/expat.h>
#include "PlatformData.h"
#include "CameraProfiles.h"

namespace android {

CameraProfiles::CameraProfiles()
{
    mCurrentSensor = 0;
    mCurrentDataField = FIELD_INVALID;
    mSensorNum = 0;
    pCurrentCam = NULL;
    LOG1("@%s", __func__);

    getDataFromXmlFile();
//    dump();
}

/**
 * This function will check which field that the parser parses to.
 *
 * The field is set to 4 types.
 * FIELD_INVALID FIELD_SENSOR_BACK FIELD_SENSOR_FRONT and FIELD_COMMON
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::checkField(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOG1("@%s, name:%s", __func__, name);

    if (strcmp(name, "CameraSettings") == 0) {
        profiles->mCurrentDataField = FIELD_INVALID;
        return;
    } else if (strcmp(name, "Profiles") == 0
            && strcmp(atts[0], "cameraId") == 0) {
        profiles->mSensorNum++;
        profiles->mCurrentSensor = atoi(atts[1]);
        if (0 == profiles->mCurrentSensor || 1 == profiles->mCurrentSensor) {
            profiles->pCurrentCam = new CameraInfo;
            if (NULL == profiles->pCurrentCam) {
                LOGE("@%s, Cannot create CameraInfo!", __func__);
                return;
            }
        }
        if (0 == profiles->mCurrentSensor) {
            profiles->mCurrentDataField = FIELD_SENSOR_BACK;
            return;
        }
        else if (1 == profiles->mCurrentSensor) {
            profiles->mCurrentDataField = FIELD_SENSOR_FRONT;
            return;
        }
    } else if (strcmp(name, "Common") == 0) {
        profiles->mCurrentDataField = FIELD_COMMON;
        return;
    }

    LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
    return;
}

/**
 * This function will handle all the common related elements.
 *
 * It will be called in the function startElement
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleCommon(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOG1("@%s, name:%s, atts[0]:%s", __func__, name, atts[0]);

    if (strcmp(atts[0], "value") != 0) {
        LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
        return;
    }

    if (strcmp(name, "subDevName") == 0) {
        PlatformBase::mSubDevName = atts[1];
    } else if (strcmp(name, "fileInject") == 0) {
        PlatformBase::mFileInject = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "backFlash") == 0) {
        PlatformBase::mBackFlash = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "continuousCapture") == 0) {
        PlatformBase::mContinuousCapture = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "productName") == 0) {
        PlatformBase::mProductName = atts[1];
    } else if (strcmp(name, "manufacturerName") == 0) {
        PlatformBase::mManufacturerName = atts[1];
    } else if (strcmp(name, "maxZoomFactor") == 0) {
        PlatformBase::mMaxZoomFactor = atoi(atts[1]);
    } else if (strcmp(name, "supportVideoSnapshot") == 0) {
        PlatformBase::mSupportVideoSnapshot = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "numRecordingBuffers") == 0) {
        PlatformBase::mNumRecordingBuffers = atoi(atts[1]);
    } else if (strcmp(name, "maxContinuousRawRingBuffer") == 0) {
        PlatformBase::mMaxContinuousRawRingBuffer = atoi(atts[1]);
    } else if (strcmp(name, "videoPreviewSizePref") == 0) {
        PlatformBase::mVideoPreviewSizePref = atts[1];
    } else if (strcmp(name, "boardName") == 0) {
        PlatformBase::mBoardName = atts[1];
    } else if (strcmp(name, "supportAIQ") == 0) {
        PlatformBase::mSupportAIQ = ((strcmp(atts[1], "true") == 0) ? true : false);
    }
}

/**
 * This function will handle all the sensor related elements.
 *
 * It will be called in the function startElement
 *
 * \param profiles: the pointer of the CameraProfiles.
 * \param name: the element's name.
 * \param atts: the element's attribute.
 */
void CameraProfiles::handleSensor(CameraProfiles *profiles, const char *name, const char **atts)
{
    LOG1("@%s, name:%s, atts[0]:%s, profiles->mCurrentSensor:%d", __func__, name, atts[0], profiles->mCurrentSensor);

    if (strcmp(atts[0], "value") != 0) {
        LOGE("@%s, name:%s, atts[0]:%s, xml format wrong", __func__, name, atts[0]);
        return;
    }

    if (strcmp(name, "maxEV") == 0) {
        pCurrentCam->maxEV = atts[1];
    } else if (strcmp(name, "minEV") == 0) {
        pCurrentCam->minEV = atts[1];
    } else if (strcmp(name, "stepEV") == 0) {
        pCurrentCam->stepEV = atts[1];
    } else if (strcmp(name, "supportedPreviewSizes") == 0) {
        pCurrentCam->supportedPreviewSizes = atts[1];
    } else if (strcmp(name, "supportedVideoSizes") == 0) {
        pCurrentCam->supportedVideoSizes = atts[1];
    } else if (strcmp(name, "supportedSceneModes") == 0) {
        pCurrentCam->supportedSceneModes = atts[1];
    } else if (strcmp(name, "defaultSceneMode") == 0) {
        pCurrentCam->defaultSceneMode = atts[1];
    } else if (strcmp(name, "sensorType") == 0) {
        pCurrentCam->sensorType = ((strcmp(atts[1], "SENSOR_TYPE_RAW") == 0) ? SENSOR_TYPE_RAW : SENSOR_TYPE_SOC);
    } else if (strcmp(name, "facing") == 0) {
        pCurrentCam->facing = ((strcmp(atts[1], "CAMERA_FACING_FRONT") == 0) ? CAMERA_FACING_FRONT : CAMERA_FACING_BACK);
    } else if (strcmp(name, "orientation") == 0) {
        pCurrentCam->orientation = atoi(atts[1]);
    } else if (strcmp(name, "dvs") == 0) {
        pCurrentCam->dvs = ((strcmp(atts[1], "true") == 0) ? true : false);
    } else if (strcmp(name, "flipping") == 0) {
        if (strcmp(atts[0], "value") == 0 && strcmp(atts[1], "SENSOR_FLIP_H") == 0)
            pCurrentCam->flipping |= PlatformData::SENSOR_FLIP_H;
        if (strcmp(atts[2], "value_v") == 0 && strcmp(atts[3], "SENSOR_FLIP_V") == 0)
            pCurrentCam->flipping |= PlatformData::SENSOR_FLIP_V;
    } else if (strcmp(name, "maxSnapshotWidth") == 0) {
        pCurrentCam->maxSnapshotWidth = atoi(atts[1]);
    } else if (strcmp(name, "maxSnapshotHeight") == 0) {
        pCurrentCam->maxSnapshotHeight = atoi(atts[1]);
    } else if (strcmp(name, "defaultBurstLength") == 0) {
        pCurrentCam->defaultBurstLength = atts[1];
    } else if (strcmp(name, "supportedBurstLength") == 0) {
        pCurrentCam->supportedBurstLength = atts[1];
    } else if (strcmp(name, "defaultFlashMode") == 0) {
        pCurrentCam->defaultFlashMode = atts[1];
    } else if (strcmp(name, "supportedFlashModes") == 0) {
        pCurrentCam->supportedFlashModes = atts[1];
    } else if (strcmp(name, "supportedEffectModes") == 0) {
        pCurrentCam->supportedEffectModes = atts[1];
    } else if (strcmp(name, "supportedIntelEffectModes") == 0) {
        pCurrentCam->supportedIntelEffectModes = atts[1];
    } else if (strcmp(name, "supportedAwbModes") == 0) {
        pCurrentCam->supportedAwbModes = atts[1];
    } else if (strcmp(name, "defaultAwbMode") == 0) {
        pCurrentCam->defaultAwbMode = atts[1];
    } else if (strcmp(name, "defaultIso") == 0) {
        pCurrentCam->defaultIso = atts[1];
    } else if (strcmp(name, "supportedIso") == 0) {
        pCurrentCam->supportedIso = atts[1];
    } else if (strcmp(name, "defaultAeMetering") == 0) {
        pCurrentCam->defaultAeMetering = atts[1];
    } else if (strcmp(name, "supportedAeMetering") == 0) {
        pCurrentCam->supportedAeMetering = atts[1];
    } else if (strcmp(name, "defaultFocusMode") == 0) {
        pCurrentCam->defaultFocusMode = atts[1];
    } else if (strcmp(name, "supportedFocusModes") == 0) {
        pCurrentCam->supportedFocusModes = atts[1];
    } else if (strcmp(name, "maxBurstFPS") == 0) {
        pCurrentCam->maxBurstFPS = atoi(atts[1]);
    } else if (strcmp(name, "supportedBurstFPS") == 0) {
        pCurrentCam->supportedBurstFPS = atts[1];
    } else if (strcmp(name, "previewViaOverlay") == 0) {
        pCurrentCam->mPreviewViaOverlay = ((strcmp(atts[1], "true") == 0) ? true : false);
    }
}

/**
 * the callback function of the libexpat for handling of one element start
 *
 * When it comes to the start of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void CameraProfiles::startElement(void *userData, const char *name, const char **atts)
{
    CameraProfiles *profiles = (CameraProfiles *)userData;

    if (profiles->mCurrentDataField == FIELD_INVALID) {
        profiles->checkField(profiles, name, atts);
        return;
    }

    switch (profiles->mCurrentDataField) {
        case FIELD_SENSOR_BACK:
        case FIELD_SENSOR_FRONT:
            profiles->handleSensor(profiles, name, atts);
            break;
        case FIELD_COMMON:
            profiles->handleCommon(profiles, name, atts);
            break;
        default:
            LOGE("@%s, line:%d, go to default handling", __func__, __LINE__);
            break;
    }
}

/**
 * the callback function of the libexpat for handling of one element end
 *
 * When it comes to the end of one element. This function will be called.
 *
 * \param userData: the pointer we set by the function XML_SetUserData.
 * \param name: the element's name.
 */
void CameraProfiles::endElement(void *userData, const char *name)
{
    CameraProfiles *profiles = (CameraProfiles *)userData;

    if (strcmp(name, "Profiles") == 0) {
        profiles->mCurrentDataField = FIELD_INVALID;
        if (profiles->pCurrentCam) {
            profiles->mCameras.push(*(profiles->pCurrentCam));
            delete profiles->pCurrentCam;
            profiles->pCurrentCam = NULL;
        }
    }
    if (strcmp(name, "Common") == 0)
        profiles->mCurrentDataField = FIELD_INVALID;
}

/**
 * Get camera configuration from xml file
 *
 * The function will read the xml configuration file firstly.
 * Then it will parse out the camera settings.
 * The camera setting is stored inside this CameraProfiles class.
 *
 */
void CameraProfiles::getDataFromXmlFile(void)
{
    int done;
    void *pBuf = NULL;
    FILE *fp = NULL;
    LOG1("@%s", __func__);

    static const char *defaultXmlFile = "/etc/camera_profiles.xml";

    fp = ::fopen(defaultXmlFile, "r");
    if (NULL == fp) {
        LOGE("@%s, line:%d, fp is NULL", __func__, __LINE__);
        return;
    }

    XML_Parser parser = ::XML_ParserCreate(NULL);
    if (NULL == parser) {
        LOGE("@%s, line:%d, parser is NULL", __func__, __LINE__);
        goto exit;
    }
    ::XML_SetUserData(parser, this);
    ::XML_SetElementHandler(parser, startElement, endElement);

    pBuf = malloc(mBufSize);
    if (NULL == pBuf) {
        LOGE("@%s, line:%d, pBuf is NULL", __func__, __LINE__);
        goto exit;
    }

    do {
        int len = (int)::fread(pBuf, 1, mBufSize, fp);
        if (!len) {
            if (ferror(fp)) {
                clearerr(fp);
                goto exit;
            }
        }
        done = len < mBufSize;
        if (XML_Parse(parser, (const char *)pBuf, len, done) == XML_STATUS_ERROR) {
            LOGE("@%s, line:%d, XML_Parse error", __func__, __LINE__);
            goto exit;
        }
    } while (!done);

exit:
    if (parser)
        ::XML_ParserFree(parser);
    if (pBuf)
        free(pBuf);
    if (fp)
    ::fclose(fp);
}

void CameraProfiles::dump(void)
{
    for (unsigned i = 0; i < getSensorNum(); i++) {
        LOGD("line%d, in DeviceData, start i:%d, sensor number:%d", __LINE__, i, getSensorNum());
        LOGD("line%d, in DeviceData, pcam->maxEV:%s ", __LINE__, mCameras[i].maxEV.string());
        LOGD("line%d, in DeviceData, pcam->minEV:%s ", __LINE__, mCameras[i].minEV.string());
        LOGD("line%d, in DeviceData, pcam->stepEV:%s ", __LINE__, mCameras[i].stepEV.string());
        LOGD("line%d, in DeviceData, pcam->supportedSceneModes:%s ", __LINE__, mCameras[i].supportedSceneModes.string());
        LOGD("line%d, in DeviceData, pcam->defaultSceneMode:%s ", __LINE__, mCameras[i].defaultSceneMode.string());
        LOGD("line%d, in DeviceData, pcam->supportedPreviewSizes:%s ", __LINE__, mCameras[i].supportedPreviewSizes.string());
        LOGD("line%d, in DeviceData, mSupportedVideoSizes:%s ", __LINE__, mCameras[i].supportedVideoSizes.string());
        LOGD("line%d, in DeviceData, pcam->maxBurstFPS:%d ", __LINE__, mCameras[i].maxBurstFPS);
        LOGD("line%d, in DeviceData, pcam->supportedBurstFPS:%s ", __LINE__, mCameras[i].supportedBurstFPS.string());
        LOGD("line%d, in DeviceData, pcam->orientation:%d ", __LINE__, mCameras[i].orientation);
        LOGD("line%d, in DeviceData, pcam->sensorType:%d ", __LINE__, mCameras[i].sensorType);
        LOGD("line%d, in DeviceData, pcam->dvs:%d ", __LINE__, mCameras[i].dvs);
        LOGD("line%d, in DeviceData, pcam->maxSnapshotWidth:%d ", __LINE__, mCameras[i].maxSnapshotWidth);
        LOGD("line%d, in DeviceData, pcam->maxSnapshotHeight:%d ", __LINE__, mCameras[i].maxSnapshotHeight);
        LOGD("line%d, in DeviceData, pcam->flipping:%d ", __LINE__, mCameras[i].flipping);
        LOGD("line%d, in DeviceData, pcam->mPreviewViaOverlay:%d ", __LINE__, mCameras[i].mPreviewViaOverlay);
        LOGD("line%d, in DeviceData, pcam->supportedBurstLength:%s ", __LINE__, mCameras[i].supportedBurstLength.string());
        LOGD("line%d, in DeviceData, pcam->facing:%d ", __LINE__, mCameras[i].facing);
        LOGD("line%d, in DeviceData, pcam->defaultBurstLength:%s ", __LINE__, mCameras[i].defaultBurstLength.string());
        LOGD("line%d, in DeviceData, pcam->defaultFlashMode:%s ", __LINE__, mCameras[i].defaultFlashMode.string());
        LOGD("line%d, in DeviceData, pcam->supportedFlashModes:%s ", __LINE__, mCameras[i].supportedFlashModes.string());
        LOGD("line%d, in DeviceData, pcam->supportedEffectModes:%s ", __LINE__, mCameras[i].supportedEffectModes.string());
        LOGD("line%d, in DeviceData, pcam->supportedIntelEffectModes:%s ", __LINE__, mCameras[i].supportedIntelEffectModes.string());
        LOGD("line%d, in DeviceData, pcam->supportedAwbModes:%s ", __LINE__, mCameras[i].supportedAwbModes.string());
        LOGD("line%d, in DeviceData, pcam->defaultAwbMode:%s ", __LINE__, mCameras[i].defaultAwbMode.string());
        LOGD("line%d, in DeviceData, pcam->defaultIso:%s ", __LINE__, mCameras[i].defaultIso.string());
        LOGD("line%d, in DeviceData, pcam->supportedIso:%s ", __LINE__, mCameras[i].supportedIso.string());
        LOGD("line%d, in DeviceData, pcam->defaultAeMetering:%s ", __LINE__, mCameras[i].defaultAeMetering.string());
        LOGD("line%d, in DeviceData, pcam->supportedAeMetering:%s ", __LINE__, mCameras[i].supportedAeMetering.string());
        LOGD("line%d, in DeviceData, pcam->defaultFocusMode:%s ", __LINE__, mCameras[i].defaultFocusMode.string());
        LOGD("line%d, in DeviceData, pcam->supportedFocusModes:%s ", __LINE__, mCameras[i].supportedFocusModes.string());
    }

    LOGD("line%d, in DeviceData, for common settings ", __LINE__);
    LOGD("line%d, in DeviceData, mSubDevName:%s ", __LINE__, mSubDevName.string());
    LOGD("line%d, in DeviceData, mFileInject:%d ", __LINE__, mFileInject);
    LOGD("line%d, in DeviceData, mBackFlash:%d ", __LINE__, mBackFlash);
    LOGD("line%d, in DeviceData, mContinuousCapture:%d ", __LINE__, mContinuousCapture);
    LOGD("line%d, in DeviceData, mVideoPreviewSizePref:%s ", __LINE__, mVideoPreviewSizePref.string());
    LOGD("line%d, in DeviceData, mProductName:%s ", __LINE__, mProductName.string());
    LOGD("line%d, in DeviceData, mManufacturerName:%s ", __LINE__, mManufacturerName.string());
    LOGD("line%d, in DeviceData, mMaxZoomFactor:%d ", __LINE__, mMaxZoomFactor);
    LOGD("line%d, in DeviceData, mSupportVideoSnapshot:%d ", __LINE__, mSupportVideoSnapshot);
    LOGD("line%d, in DeviceData, mNumRecordingBuffers:%d ", __LINE__, mNumRecordingBuffers);
    LOGD("line%d, in DeviceData, mMaxContinuousRawRingBuffer:%d ", __LINE__, mMaxContinuousRawRingBuffer);
    LOGD("line%d, in DeviceData, mBoardName:%s ", __LINE__, mBoardName.string());
    LOGD("line%d, in DeviceData, mSupportAIQ:%d ", __LINE__, mSupportAIQ);
}

}