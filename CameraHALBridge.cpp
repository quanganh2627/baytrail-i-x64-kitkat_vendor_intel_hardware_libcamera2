/*
**
** Copyright 2008, The Android Open Source Project
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
#include <linux/videodev2.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include "CameraHardware.h"
#include "CameraHardwareSOC.h"

#define MAX_CAMERAS 2		// Follow CamreaService.h
#define SENSOR_TYPE_SOC 0
#define SENSOR_TYPE_RAW 1

namespace android {

static int HAL_cameraType[MAX_CAMERAS];
static CameraInfo HAL_cameraInfo[MAX_CAMERAS] = {
	{
		CAMERA_FACING_BACK,
		90,  /* orientation */
	},
	{
		CAMERA_FACING_FRONT,
		0,  /* orientation */
	}
};

extern "C" int HAL_checkCameraType(unsigned char *name) {

    //TODO: It's a bad way to judge sensor type by its name.
    //We need to find a better way in future.
    if(strstr((const char *)name, "soc"))
    {
        LOGD("%s:: Here is SOC CAMERA SENSOR, named %s", __FUNCTION__, name);
        return SENSOR_TYPE_SOC;
    }
    else
    {
        LOGD("%s:: Here is RAW CAMERA SENSOR, named %s", __FUNCTION__, name);
        return SENSOR_TYPE_RAW;
    }
}

/* This function will be called when the camera service is created.
 * Do some init work in this function.
 */
extern "C" int HAL_getNumberOfCameras()
{
	int fd;
	int type;
	fd = open("/dev/video0", O_RDWR);

	if (-1 == fd) {
		LOGE("Error opening video device /dev/video0");
		return 0;
	}

	/* enumerate input */
	struct v4l2_input input;
	int index;

	memset(&input, 0, sizeof (input));
	input.index = 0;

	if (-1 == ioctl(fd, VIDIOC_ENUMINPUT, &input)) {
		LOGE("no sensor input available!");
		close(fd);
		return 0;
	}
	type = HAL_checkCameraType(input.name);
	HAL_cameraType[0] = type;
	LOGI("Input %d (%s)", input.index, input.name);

	memset(&input, 0, sizeof (input));
	input.index = 1;

	if (-1 == ioctl(fd, VIDIOC_ENUMINPUT, &input)) {
		LOGI("Only 1 sensor is connected.");
		close(fd);
		return 1;
	}
	type = HAL_checkCameraType(input.name);
	HAL_cameraType[1] = type;
	LOGI("Input %d (%s)", input.index, input.name);

	close(fd);
	return 2;
}

extern "C" void HAL_getCameraInfo(int cameraId, struct CameraInfo* cameraInfo)
{
	memcpy(cameraInfo, &HAL_cameraInfo[cameraId], sizeof(CameraInfo));
}

extern "C" sp<CameraHardwareInterface> HAL_openCameraHardware(int cameraId)
{
	// TODO: set input according to cameraId

	if (HAL_cameraType[cameraId] == SENSOR_TYPE_RAW)
		return CameraHardware::createInstance(cameraId);
	else if (HAL_cameraType[cameraId] == SENSOR_TYPE_SOC)
		return CameraHardwareSOC::createInstance(cameraId);
	else
		return NULL;
}

}; // namespace android

