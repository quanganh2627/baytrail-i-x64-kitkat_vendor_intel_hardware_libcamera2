/*
**
** Copyright 2012, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include "OlaFaceDetect.h"
#include <stdlib.h>
#include <system/camera.h>
#include "IFaceDetectionListener.h"
#include "AtomCommon.h"
#include "AtomAAA.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "Camera_OlaFaceDetect"
//#define LOG_NDEBUG 0
#include "LogHelper.h"

namespace android {

static void useFacesForAAA(const camera_frame_metadata_t& face_metadata);

OlaFaceDetect::OlaFaceDetect(IFaceDetectionListener *pListener) :
            IFaceDetector(pListener),
            mMessageQueue("OlaFaceDetector"),
            mFaceDetectionStruct(0),
            mbRunning(false),
            mLastReportedNumberOfFaces(0)
{
}

OlaFaceDetect::~OlaFaceDetect()
{
    LOGV("%s: Destroy the OlaFaceDetec\n", __func__);

    mbRunning = false;
    if (mFaceDetectionStruct)
        CameraFaceDetection_Destroy(&mFaceDetectionStruct);
    mFaceDetectionStruct = 0;

    LOGV("%s: Destroy the OlaFaceDetec DONE.\n", __func__);
}

void OlaFaceDetect::start()
{
    LOGV("%s: START Face Detection mFaceDetectionStruct 0x%p\n", __func__, mFaceDetectionStruct);
    int ret = 0;

    // Since clients can stop the thread asynchronously with stop(wait=false)
    // there is a chance that the thread didn't wake up to process MESSAGE_ID_EXIT.
    // In that case, let's just remove the MESSAGE_ID_EXIT message from the queue
    // and let it keep running.
    mMessageQueue.remove(MESSAGE_ID_EXIT);
    if (!mFaceDetectionStruct) {
        ret = CameraFaceDetection_Create(&mFaceDetectionStruct);
        if (!ret) {
            mbRunning = true;
            run();
        }
        LOGV("%s: Ola Face Detection struct Created. Ret: %d struct: 0x%p", __func__,ret, mFaceDetectionStruct);
    }else{
        mbRunning = true;
        run();//restart thread
    }
}

void OlaFaceDetect::stop(bool wait)
{
    LOGV("%s: STOP Face DEtection mFaceDetectionStruct 0x%p\n", __func__, mFaceDetectionStruct);
    Message msg;
    msg.id = MESSAGE_ID_EXIT;
    mMessageQueue.remove(MESSAGE_ID_FRAME); // flush all buffers
    mMessageQueue.send( &msg );
    if (wait) {
        requestExitAndWait();
    }else
        requestExit();
}

status_t OlaFaceDetect::handleExit()
{
    LOGV("%s: Stop Face Detection\n", __func__);
    int ret = 0;
    mbRunning = false;
    return NO_ERROR;
}

int OlaFaceDetect::sendFrame(AtomBuffer *img, int width, int height)
{
    LOGV("%s: sendFrame, data =%p, width=%d height=%d\n", __func__, img->buff->data, width, height);
    Message msg;
    msg.id = MESSAGE_ID_FRAME;
    msg.data.frame.img = *img;
    msg.data.frame.height = height;
    msg.data.frame.width = width;
    if (mMessageQueue.send(&msg) == NO_ERROR) {
        return 0;
    }
    else
        return -1;
}

bool OlaFaceDetect::threadLoop()
{
    status_t status = NO_ERROR;
    Message msg;
    while(mbRunning)
    {
        LOGV("getting message....");
        mMessageQueue.receive(&msg);
        LOGV("operation message ID = %d", msg.id);
        switch (msg.id)
        {
        case MESSAGE_ID_FRAME:
            status = handleFrame(msg.data.frame);
            break;
        case MESSAGE_ID_EXIT:
            status = handleExit();
            break;
        default:
            status = INVALID_OPERATION;
            break;
        }
        if (status != NO_ERROR) {
            LOGE("operation failed, status = %d", status);
        }
    }
    return false;
}
status_t OlaFaceDetect::handleFrame(MessageFrame frame)
{
    LOGV("%s: Face detection executing\n", __func__);
    if (mFaceDetectionStruct == 0) return INVALID_OPERATION;

    LOGV("%s: data =%p, width=%d height=%d\n", __func__, frame.img.buff->data, frame.width, frame.height);
    int faces = CameraFaceDetection_FindFace(mFaceDetectionStruct,
            (unsigned char*) (frame.img.buff->data),
            frame.width, frame.height);
    LOGV("%s CameraFaceDetection_FindFace faces %d, %d\n", __func__, faces, mFaceDetectionStruct->numDetected);

    camera_frame_metadata_t face_metadata;
    face_metadata.number_of_faces = mFaceDetectionStruct->numDetected;
    face_metadata.faces = (camera_face_t *)mFaceDetectionStruct->detectedFaces;
    for (int i=0; i<face_metadata.number_of_faces;i++) {
        camera_face_t& face =face_metadata.faces[i];
        LOGV("face id=%d, score =%d", face.id, face.score);
        LOGV("rect = (%d, %d, %d, %d)",face.rect[0],face.rect[1],
                face.rect[2],face.rect[3]);
        LOGV("mouth: (%d, %d)",face.mouth[0], face.mouth[1]);
        LOGV("left eye: (%d, %d)", face.left_eye[0], face.left_eye[1]);
        LOGV("right eye: (%d, %d)", face.right_eye[0], face.right_eye[1]);
    }
    //blocking call
    LOGV("%s calling listener", __func__);
    if((face_metadata.number_of_faces > 0) ||
       (mLastReportedNumberOfFaces != 0)) {

        mLastReportedNumberOfFaces = face_metadata.number_of_faces;
        mpListener->facesDetected(face_metadata);
    }

    LOGV("%s returned from listener", __func__);

    useFacesForAAA(face_metadata);
    if (frame.img.owner != 0) {
        frame.img.owner->returnBuffer(&frame.img);
    }

    return NO_ERROR;
}

static void setFocusAreas(const CameraWindow* windows, size_t winCount)
{
    AfMode newAfMode = CAM_AF_MODE_TOUCH;

    AtomAAA* aaa = AtomAAA::getInstance();
    if (aaa->setAfWindows(windows, winCount) == NO_ERROR) {
        // See if we have to change the actual mode (it could be correct already)
        AfMode curAfMode = aaa->getAfMode();
        if (curAfMode != newAfMode)
            aaa->setAfMode(newAfMode);
    }
    return;
}

void useFacesForAAA(const camera_frame_metadata_t& face_metadata)
{
    if (face_metadata.number_of_faces <=0) return;
    CameraWindow * windows = new CameraWindow[face_metadata.number_of_faces];
    for (int i=0; i<face_metadata.number_of_faces;i++) {
         camera_face_t& face =face_metadata.faces[i];
         LOG2("face id=%d, score =%d", face.id, face.score);
         LOG2("rect = (%d, %d, %d, %d)",face.rect[0],face.rect[1],
                 face.rect[2],face.rect[3]);
         windows[i].x_left = face.rect[0];
         windows[i].y_top = face.rect[1];
         windows[i].x_right = face.rect[2];
         windows[i].y_bottom = face.rect[3];
         LOG2("mouth: (%d, %d)",face.mouth[0], face.mouth[1]);
         LOG2("left eye: (%d, %d)", face.left_eye[0], face.left_eye[1]);
         LOG2("right eye: (%d, %d)", face.right_eye[0], face.right_eye[1]);
     }

    //TODO: spec says we need also do AWB and AE. Currently no support.
    //JIRA created:ANDROID-1838
    setFocusAreas(windows, face_metadata.number_of_faces);
}

}
