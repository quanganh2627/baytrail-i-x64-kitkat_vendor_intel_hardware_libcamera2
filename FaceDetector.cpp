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

namespace android {

FaceDetector::FaceDetector():

    mSmileThreshold(0)
    ,mBlinkThreshold(0)
{
    LOG1("@%s", __FUNCTION__);
    mContext = ia_face_init(NULL);
}

FaceDetector::~FaceDetector()
{
    LOG1("@%s", __FUNCTION__);
    ia_face_uninit(mContext);
    mContext = NULL;
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

    mSmileThreshold = threshold;
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
    if (face.smile_state != 0 && face.smile_score > mSmileThreshold) {
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
    if (threshold > 0)
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

}; // namespace android

#endif // ENABLE_INTEL_EXTRAS
