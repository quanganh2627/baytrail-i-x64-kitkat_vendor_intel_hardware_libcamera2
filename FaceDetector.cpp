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
#define LOG_TAG "Camera_FaceDetector"
//#define LOG_NDEBUG 0

#ifdef ENABLE_INTEL_EXTRAS

#include "FaceDetector.h"
#include "LogHelper.h"
#include "sqlite3.h"
#include "cutils/properties.h"
#include "AtomISP.h"
#include "AtomCommon.h"
#include "AtomAcc.h"

#include "ia_coordinate.h"

namespace android {

FaceDetector::FaceDetector() : Thread()
    ,mContext(ia_face_init(NULL))
    ,mMessageQueue("FaceDetector", (int) MESSAGE_ID_MAX)
    ,mSmileThreshold(0)
    ,mBlinkThreshold(0)
    ,mFaceRecognitionRunning(false)
    ,mAccHandle(0)
    ,mThreadRunning(false)
{
    LOG1("@%s", __FUNCTION__);
}

FaceDetector::~FaceDetector()
{
    LOG1("@%s", __FUNCTION__);
    ia_face_uninit(mContext);
    mContext = NULL;
}

void FaceDetector::setAcc(void* isp)
{
    LOG2("@%s", __FUNCTION__);
    ia_acceleration accApi;
    ia_acceleration *accApiPtr = NULL;

    if (isp == mAccHandle)
        return;

    if (isp) {
        accApi.isp               = isp;
        accApi.open_firmware     = open_firmware;
        accApi.load_firmware     = load_firmware_pipe; // beware this is not just load_firmware.
        accApi.unload_firmware   = unload_firmware;
        accApi.set_firmware_arg  = set_firmware_arg;
        accApi.start_firmware    = start_firmware;
        accApi.wait_for_firmware = wait_for_firmware;
        accApi.abort_firmware    = abort_firmware;
        accApi.map_firmware_arg   = map_firmware_arg;
        accApi.unmap_firmware_arg = unmap_firmware_arg;
        accApi.set_mapped_arg     = set_mapped_arg;
        accApiPtr = &accApi;
    }

    // note: passing a NULL accApiPtr value closes
    //       all previously allocated ISP resources
    ia_face_set_acceleration(mContext, accApiPtr);
    mAccHandle = isp;
}

int FaceDetector::faceDetect(ia_frame *frame)
{
    LOG2("@%s", __FUNCTION__);
    ia_face_detect(mContext, frame);
    return mContext->num_faces;
}

void FaceDetector::eyeDetect(ia_frame *frame)
{
    LOG2("@%s", __FUNCTION__);
    ia_face_eye_detect(mContext, frame);
}

void FaceDetector::setSmileThreshold(int threshold)
{
    LOG1("@%s", __FUNCTION__);
    ia_face_parameters faceLibParams;
    if (threshold >= 0) {
        ia_face_get_parameters(mContext, &faceLibParams);
        faceLibParams.smile_threshold = threshold;
        ia_face_set_parameters(mContext, &faceLibParams);
    }
}

bool FaceDetector::smileDetect(ia_frame *frame)
{
    LOG2("@%s", __FUNCTION__);
    ia_face_smile_detect(mContext, frame);

    // All detected faces have to smile for positive detection
    bool smile = false;
    for (int i = 0; i < mContext->num_faces; i++)
    {
        ia_face face = mContext->faces[i];
        if (face.smile_state == SMILE) {
            smile = true;
        } else {
            smile = false;
            break;
        }
    }
    return smile;
}

void FaceDetector::setBlinkThreshold(int threshold)
{
    LOG1("@%s", __FUNCTION__);
    if (threshold >= 0)
        mBlinkThreshold = threshold;
}

bool FaceDetector::blinkDetect(ia_frame *frame)
{
    LOG2("@%s", __FUNCTION__);
    ia_face_blink_detect(mContext, frame);

    // None of the detected faces should have eyes blinked
    bool blink = true;
    for (int i = 0; i < mContext->num_faces; i++)
    {
        ia_face face = mContext->faces[i];
        if (face.left_eye.blink_confidence < mBlinkThreshold  &&
            face.right_eye.blink_confidence < mBlinkThreshold) {
            blink = false;
        } else {
            blink = true;
            break;
        }
    }
    return blink;
}

status_t FaceDetector::startFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_START_FACE_RECOGNITION;
    mMessageQueue.send(&msg);
    return status;
}

status_t FaceDetector::handleMessageStartFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mFaceRecognitionRunning) {
        LOGE("@%s: face recognition already running", __FUNCTION__);
        return INVALID_OPERATION;
    }

    status = loadFaceDb();
    if (status == NO_ERROR) {
        mFaceRecognitionRunning = true;
    } else {
        LOGE("loadFaceDb() failed: %x", status);
        status = UNKNOWN_ERROR;
    }
    return status;
}

status_t FaceDetector::stopFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_STOP_FACE_RECOGNITION;
    mMessageQueue.send(&msg);
    return status;
}

status_t FaceDetector::handleMessageStopFaceRecognition()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mFaceRecognitionRunning = false;
    return status;
}


status_t FaceDetector::reset()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    msg.id = MESSAGE_ID_RESET;
    mMessageQueue.send(&msg);
    return status;
}

status_t FaceDetector::handleMessageReset()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;

    if (mContext != NULL)
        ia_face_reinit(mContext);
    else
        status = INVALID_OPERATION;

    return status;
}

void FaceDetector::faceRecognize(ia_frame *frame)
{
    LOG2("@%s", __FUNCTION__);
    if (mFaceRecognitionRunning && mContext->num_faces > 0)
        ia_face_recognize(mContext, frame);
}

status_t FaceDetector::loadFaceDb()
{
    LOG1("@%s", __FUNCTION__);
    int ret;
    sqlite3 *pDb;
    sqlite3_stmt *pStmt;
    int featureId, version, personId, timeStamp;
    const void* feature;
    int featureCount = 0;
    char dbPath[150];

    // Get face DB path from system property if available, or use the default path
    property_get("gallery.dbpath", dbPath, PERSONDB_DEFAULT_PATH);
    strcat(dbPath, "/");
    strcat(dbPath, PERSONDB_FILENAME);
    LOG1("@%s: Opening face DB from: %s", __FUNCTION__, dbPath);

    ret = sqlite3_open(dbPath, &pDb);
    if (ret != SQLITE_OK) {
        LOGE("sqlite3_open error : %s", sqlite3_errmsg(pDb));
        return UNKNOWN_ERROR;
    }

    const char *select_query = "SELECT featureId, version, personId, feature, timeStamp FROM Feature";
    ret = sqlite3_prepare_v2(pDb, select_query, -1, &pStmt, NULL);
    if (ret != SQLITE_OK) {
        LOGE("sqlite3_prepare_v2 error : %s", sqlite3_errmsg(pDb));
        sqlite3_close(pDb);
        return UNKNOWN_ERROR;
    }

    while (sqlite3_step(pStmt) == SQLITE_ROW) {
        featureId = sqlite3_column_int(pStmt, 0);
        version = sqlite3_column_int(pStmt, 1);
        personId = sqlite3_column_int(pStmt, 2);
        feature = sqlite3_column_blob(pStmt, 3);
        timeStamp = sqlite3_column_int(pStmt, 4);
        ret = ia_face_register_feature(mContext, (uint8_t*)feature, personId, featureId, timeStamp, 0, 0, version);
        LOG2("Register feature (%d): face ID: %d, feature ID: %d, timestamp: %d, version: %d", featureCount, personId, featureId, timeStamp, version);
        if (ret < 0) {
            LOGE("Error on loading feature data(%d) : %d", featureCount, ret);
        }
        featureCount++;
    }

    sqlite3_finalize(pStmt);
    sqlite3_close(pDb);

    return NO_ERROR;
}


/**
 * Converts the detected faces from ia_face format to Google format.
 *
 * @param faces_out: [OUT]: Detected faces in Google format
 * @param width: [IN]: Width of the preview frame
 * @param height: [IN]: Height of the preview frame
 *
 * @return Number of faces
 */
int FaceDetector::getFaces(camera_face_t *faces_out, int width, int height)
{
    LOG2("@%s", __FUNCTION__);

    // Coordinate range defined in camera_face_t: [-1000 ... 1000]
    const int coord_range = 2000;

    for (int i = 0; i < mContext->num_faces; i++)
    {
        camera_face_t& face = faces_out[i];
        ia_face iaFace = mContext->faces[i];

        // ia_face coordinate range is [0 ... width] or [0 ... height]
        face.rect[0] = iaFace.face_area.left * coord_range / width - coord_range / 2;
        face.rect[1] = iaFace.face_area.top * coord_range / height - coord_range / 2;
        face.rect[2] = iaFace.face_area.right * coord_range / width - coord_range / 2;
        face.rect[3] = iaFace.face_area.bottom * coord_range / height - coord_range / 2;

        face.score = iaFace.confidence;
        face.id = iaFace.person_id;

        face.left_eye[0] = iaFace.left_eye.position.x * coord_range / width - coord_range / 2;
        face.left_eye[1] = iaFace.left_eye.position.y * coord_range / height - coord_range / 2;

        face.right_eye[0] = iaFace.right_eye.position.x * coord_range / width - coord_range / 2;
        face.right_eye[1] = iaFace.right_eye.position.y * coord_range / height - coord_range / 2;

        face.mouth[0] = iaFace.mouth.x * coord_range / width - coord_range / 2;
        face.mouth[1] = iaFace.mouth.y * coord_range / height - coord_range / 2;

        LOG2("face id: %d, score: %d", face.id, face.score);
        LOG2("coordinates: (%d, %d, %d, %d)",face.rect[0],face.rect[1], face.rect[2],face.rect[3]);
        LOG2("mouth: (%d, %d)",face.mouth[0], face.mouth[1]);
        LOG2("left eye: (%d, %d), blink confidence: %d, threshold %d", face.left_eye[0], face.left_eye[1],
            iaFace.left_eye.blink_confidence, mBlinkThreshold);
        LOG2("right eye: (%d, %d), blink confidence: %d, threshold %d", face.right_eye[0], face.right_eye[1],
            iaFace.right_eye.blink_confidence, mBlinkThreshold);
        LOG2("smile state: %d, score: %d, threshold %d", iaFace.smile_state, iaFace.smile_score, mSmileThreshold);
    }
    return mContext->num_faces;
}

/**
 * Return detected faces in ia_face format and ia_coordinate
 *
 * @param faces_out: [OUT]: Detected faces
 * @param width: [IN]: Width of the preview frame
 * @param height: [IN]: Height of the preview frame
 * @param zoomRatio: [IN]: digital zoomRatio of preview frame multiblied by 100
 *
 * @return Number of faces
 */
void FaceDetector::getFaceState(ia_face_state *faceStateOut, int width, int height, int zoomRatio) {
    LOG2("@%s", __FUNCTION__);

    assert(faceStateOut != NULL);

    faceStateOut->num_faces = mContext->num_faces;
    memcpy(faceStateOut->faces, mContext->faces, mContext->num_faces * sizeof(ia_face));

    // ia_face coordinate range is [0 ... width] or [0 ... height]
    ia_coordinate_system srcCoordinateSystem;
    srcCoordinateSystem.top = 0;
    srcCoordinateSystem.left = 0;
    srcCoordinateSystem.bottom = height;
    srcCoordinateSystem.right = width;

    // use zoom ratio calculate where visible frame is in ia_coorinates
    ia_coordinate_system trgCoordinateSystem;
    long targetWidth = IA_COORDINATE_WIDTH * 100 / zoomRatio;
    long targetHeight = IA_COORDINATE_HEIGHT * 100 / zoomRatio;
    trgCoordinateSystem.top = IA_COORDINATE_TOP + (IA_COORDINATE_HEIGHT - targetHeight) / 2;
    trgCoordinateSystem.left = IA_COORDINATE_LEFT + (IA_COORDINATE_WIDTH - targetWidth) / 2;
    trgCoordinateSystem.bottom = trgCoordinateSystem.top + targetHeight;
    trgCoordinateSystem.right = trgCoordinateSystem.left + targetWidth;

    ia_coordinate_convert_faces(faceStateOut, &srcCoordinateSystem, &trgCoordinateSystem);
}

bool FaceDetector::threadLoop()
{
    LOG2("@%s", __FUNCTION__);
    mThreadRunning = true;
    while (mThreadRunning)
        waitForAndExecuteMessage();

    return false;
}

status_t FaceDetector::waitForAndExecuteMessage()
{
    LOG2("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    Message msg;
    mMessageQueue.receive(&msg);

    switch (msg.id)
    {
        case MESSAGE_ID_EXIT:
            status = handleExit();
            break;
        case MESSAGE_ID_START_FACE_RECOGNITION:
            status = handleMessageStartFaceRecognition();
            break;
        case MESSAGE_ID_STOP_FACE_RECOGNITION:
            status = handleMessageStopFaceRecognition();
            break;
        case MESSAGE_ID_RESET:
            status = handleMessageReset();
            break;
        default:
            status = INVALID_OPERATION;
            break;
    }
    if (status != NO_ERROR) {
        LOGE("operation failed, ID = %d, status = %d", msg.id, status);
    }
    return status;
}

status_t FaceDetector::handleExit()
{
    LOG1("@%s", __FUNCTION__);
    status_t status = NO_ERROR;
    mThreadRunning = false;
    return status;
}

status_t FaceDetector::requestExitAndWait()
{
    LOG2("@%s", __FUNCTION__);
    Message msg;
    msg.id = MESSAGE_ID_EXIT;
    // tell thread to exit
    // send message asynchronously
    mMessageQueue.send(&msg);

    // propagate call to base class
    return Thread::requestExitAndWait();
}

}; // namespace android

#endif // ENABLE_INTEL_EXTRAS
