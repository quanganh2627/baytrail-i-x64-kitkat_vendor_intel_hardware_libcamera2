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

#define LOG_TAG "CameraHAL"
#include "CameraHardware.h"

static android::Mutex gCameraHalDeviceLock;
static int HAL_OpenCameraHardware(const hw_module_t* module,
                                  const char* name,
                                  hw_device_t** device);
static int HAL_CloseCameraHardware(hw_device_t* device);
static int HAL_GetNumberOfCameras(void);
static int HAL_GetCameraInfo(int camera_id,
                             struct camera_info *info);

static struct hw_module_methods_t camera_module_methods = {
    open: HAL_OpenCameraHardware
};

camera_module_t HAL_MODULE_INFO_SYM = {
    common: {
         tag: HARDWARE_MODULE_TAG,
         version_major: 1,
         version_minor: 0,
         id: CAMERA_HARDWARE_MODULE_ID,
         name: "Intel CameraHardware Module",
         author: "Intel",
         methods: &camera_module_methods,
         dso: NULL, /* remove compilation warnings */
         reserved: {0}, /* remove compilation warnings */
    },
    get_number_of_cameras: HAL_GetNumberOfCameras,
    get_camera_info: HAL_GetCameraInfo,
};

struct intel_camera {
    int camera_id;
    android::CameraHardware* hardware;
};


static camera_device_ops_t* dev_ops = NULL;

/* camera operations */

int camera_set_preview_window(struct camera_device * device,
        struct preview_stream_ops *window)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->setPreviewWindow(window);
}

void camera_set_callbacks(struct camera_device * device,
        camera_notify_callback notify_cb,
        camera_data_callback data_cb,
        camera_data_timestamp_callback data_cb_timestamp,
        camera_request_memory get_memory,
        void *user)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
	struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->setCallbacks(notify_cb, data_cb, data_cb_timestamp, get_memory, user);
}

void camera_enable_msg_type(struct camera_device * device, int32_t msg_type)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->enableMsgType(msg_type);
}

void camera_disable_msg_type(struct camera_device * device, int32_t msg_type)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->disableMsgType(msg_type);
}

int camera_msg_type_enabled(struct camera_device * device, int32_t msg_type)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return 0;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->msgTypeEnabled(msg_type);
}

int camera_start_preview(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->startPreview();
}

void camera_stop_preview(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->stopPreview();
}

int camera_preview_enabled(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->previewEnabled();
}

int camera_store_meta_data_in_buffers(struct camera_device * device, int enable)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    // TODO: meta data buffer not current supported
	struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->storeMetaDataInBuffers(enable);
}

int camera_start_recording(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->startRecording();
}

void camera_stop_recording(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->stopRecording();
}

int camera_recording_enabled(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->recordingEnabled();
}

void camera_release_recording_frame(struct camera_device * device,
                const void *opaque)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->releaseRecordingFrame(opaque);
}

int camera_auto_focus(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->autoFocus();
}

int camera_cancel_auto_focus(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->cancelAutoFocus();
}

int camera_take_picture(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->takePicture();
}

int camera_cancel_picture(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->cancelPicture();
}

int camera_set_parameters(struct camera_device * device, const char *params)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->setParameters(params);
}

char* camera_get_parameters(struct camera_device * device)
{
    char* params = NULL;
    LOGV("%s", __FUNCTION__);
    if(!device)
        return NULL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    params = camera_priv->hardware->getParameters();
    return params;
}

void camera_put_parameters(struct camera_device *device, char *parms)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->putParameters(parms);
}

int camera_send_command(struct camera_device * device,
            int32_t cmd, int32_t arg1, int32_t arg2)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->sendCommand(cmd, arg1, arg2);
}

void camera_release(struct camera_device * device)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    camera_priv->hardware->release();
}

int camera_dump(struct camera_device * device, int fd)
{
    LOGV("%s", __FUNCTION__);
    if(!device)
        return -EINVAL;
    struct intel_camera *camera_priv = (struct intel_camera *)(device->priv);
    return camera_priv->hardware->dump(fd);
}



int HAL_CloseCameraHardware(hw_device_t* device)
{
    LOGV("%s", __FUNCTION__);
    if (!device)
        return -EINVAL;
    android::Mutex::Autolock lock(gCameraHalDeviceLock);
    camera_device_t *camera_dev = (camera_device_t *)device;
    struct intel_camera *camera_priv = (struct intel_camera *)(camera_dev->priv);
    delete camera_priv->hardware;
    LOGD("Freeing intel_camera: %p", camera_priv);
    free(camera_priv);
    LOGD("Freeing camera_device_t: %p", camera_dev);
    free(camera_dev);
    return 0;
}

int HAL_OpenCameraHardware(const hw_module_t* module, const char* name,
                hw_device_t** device)
{
    LOGV("%s", __FUNCTION__);
    int status = -EINVAL;

    android::Mutex::Autolock lock(gCameraHalDeviceLock);

    camera_device_t *camera_dev;
    if (dev_ops == NULL) {
        dev_ops = (camera_device_ops_t*)malloc(sizeof(*dev_ops));
        memset(dev_ops, 0, sizeof(*dev_ops));
        dev_ops->set_preview_window = camera_set_preview_window;
        dev_ops->set_callbacks = camera_set_callbacks;
        dev_ops->enable_msg_type = camera_enable_msg_type;
        dev_ops->disable_msg_type = camera_disable_msg_type;
        dev_ops->msg_type_enabled = camera_msg_type_enabled;
        dev_ops->start_preview = camera_start_preview;
        dev_ops->stop_preview = camera_stop_preview;
        dev_ops->preview_enabled = camera_preview_enabled;
        dev_ops->store_meta_data_in_buffers = camera_store_meta_data_in_buffers;
        dev_ops->start_recording = camera_start_recording;
        dev_ops->stop_recording = camera_stop_recording;
        dev_ops->recording_enabled = camera_recording_enabled;
        dev_ops->release_recording_frame = camera_release_recording_frame;
        dev_ops->auto_focus = camera_auto_focus;
        dev_ops->cancel_auto_focus = camera_cancel_auto_focus;
        dev_ops->take_picture = camera_take_picture;
        dev_ops->cancel_picture = camera_cancel_picture;
        dev_ops->set_parameters = camera_set_parameters;
        dev_ops->get_parameters = camera_get_parameters;
        dev_ops->put_parameters = camera_put_parameters;
        dev_ops->send_command = camera_send_command;
        dev_ops->release = camera_release;
        dev_ops->dump = camera_dump;
    }

    camera_dev = (camera_device_t*)malloc(sizeof(*camera_dev));
    LOGD("Allocated camera_device_t: %p", camera_dev);
    struct intel_camera *camera_priv = (struct intel_camera *)malloc(sizeof(struct intel_camera));
    LOGD("Allocated intel_camera: %p", camera_priv);

    /* initialize structs */
    memset(camera_dev, 0, sizeof(*camera_dev));
    camera_priv->camera_id = atoi(name);
    camera_priv->hardware = android::CameraHardware::createInstance(camera_priv->camera_id);

    camera_dev->common.tag = HARDWARE_DEVICE_TAG;
    camera_dev->common.version = 0;
    camera_dev->common.module = (hw_module_t *)(module);
    camera_dev->common.close = HAL_CloseCameraHardware;
    camera_dev->ops = dev_ops;
    camera_dev->priv = camera_priv;

    *device = &camera_dev->common;
    status = 0;
    return status;
}

int HAL_GetNumberOfCameras(void)
{
    return android::CameraHardware::getNumberOfCameras();
}

int HAL_GetCameraInfo(int camera_id, struct camera_info *info)
{
    return android::CameraHardware::getCameraInfo(camera_id, info);
}

