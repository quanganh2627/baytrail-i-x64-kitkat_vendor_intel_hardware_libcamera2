
/*
**
** Copyright 2008, The Android Open Source Project
** Copyright 2011, Intel Corporation
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

#define LOG_TAG "Camera_IOlaBuffer"

#include <utils/Log.h>
#include <stdint.h>
#include <sys/types.h>
#include <binder/MemoryHeapBase.h>
#include <OlaService/IOlaBuffer.h>

namespace android {

/* --- Client side --- */
class BpOlaBuffer: public BpInterface<IOlaBuffer>
{
public:
	BpOlaBuffer(const sp<IBinder>& impl) : BpInterface<IOlaBuffer>(impl)
	{
	}

	sp<IMemoryHeap> getPreviewBuffer() {
		Parcel data, reply;
		sp<IMemoryHeap> memHeap = NULL;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		// This will result in a call to the onTransact()
		// method on the server in it's context (from it's binder threads)
		remote()->transact(GET_PREVIEWBUFFER, data, &reply);
		memHeap = interface_cast<IMemoryHeap> (reply.readStrongBinder());
		return memHeap;
	}

	sp<IMemoryHeap> requestBuffer(int bufferId, size_t size) {
		Parcel data, reply;
		sp<IMemoryHeap> memHeap = NULL;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		data.writeInt32(bufferId);
		data.writeInt32(size);
		remote()->transact(REQUESTBUFFER, data, &reply);
		memHeap = interface_cast<IMemoryHeap> (reply.readStrongBinder());
		return memHeap;

	}
	int	releaseBuffer(int bufferId) {
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
	//	data.writeStrongBinder(accbuffer->asBinder());
		data.writeInt32(bufferId);
		remote()->transact(RELEASEBUFFER, data, &reply);
		return 0;
	}


	int  configLoadFirmware(void) {
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		remote()->transact(CONFIG_LOAD_FIRMWARE, data, &reply);
		return reply.readInt32();
	}

	void configUnLoadFirmware(void) {
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		remote()->transact(CONFIG_UNLOAD_FIRMWARE, data, &reply);
	}

	int configStartFirmware(void) {
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		remote()->transact(CONFIG_START_FIRMWARE, data, &reply);
		return reply.readInt32();
	}

	void configDoneFirmware(void) {
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		remote()->transact(CONFIG_DONE_FIRMWARE, data, &reply);
	}

	void configAbortFirmware(void) {
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		remote()->transact(CONFIG_ABORT_FIRMWARE, data, &reply);
	}

	int  configSetArgFirmware(const int arg_ID, const void *arg, const size_t size) {
		LOGD("CLIENT::BpBuffer[%s] calling transaction arg_Id(%d), arg(%x), size(%d)", __FUNCTION__, arg_ID, (int)arg, size);
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		data.writeInt32(arg_ID);
		data.writeInt32((int)arg);
		data.writeInt32((int)size);
		remote()->transact(CONFIG_SETARG_FIRMWARE, data, &reply);
		return reply.readInt32();

	}


	int  configSetArgSharedBufferFirmware(const int arg_ID, int bufferId, const size_t size) {
		LOGD("CLIENT::BpBuffer[%s] calling transaction arg_Id(%d), bufferId(%x), size(%d)", __FUNCTION__, arg_ID, bufferId, size);
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		data.writeInt32(arg_ID);
		data.writeInt32(bufferId);
		data.writeInt32((int)size);
		remote()->transact(CONFIG_SETARG_FIRMWARE_SHAREDBUFFER, data, &reply);
		return reply.readInt32();

	}

	int  configDestabilizeArgFirmware(const int arg_ID) {
		LOGD("CLIENT::BpBuffer[%s] calling transaction arg_Id(%d)", __FUNCTION__, arg_ID);
		Parcel data, reply;
		data.writeInterfaceToken(IOlaBuffer::getInterfaceDescriptor());
		data.writeInt32(arg_ID);
		remote()->transact(CONFIG_DESTABILIZE_ARG_FIRMWARE, data, &reply);
		return reply.readInt32();

	}

private:


};

IMPLEMENT_META_INTERFACE(OlaBuffer, OLABUFFER_DESCRIPTOR);

/* --- Server side --- */

status_t BnOlaBuffer::onTransact(uint32_t code, const Parcel& data, Parcel* reply, uint32_t flags)
{
	switch (code)
	{
	case GET_PREVIEWBUFFER:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		sp<IMemoryHeap> Data = getPreviewBuffer();
		if (Data != NULL)
		{
			reply->writeStrongBinder(Data->asBinder());
		}
		return NO_ERROR;
		break;
	}
	case REQUESTBUFFER:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		int bufferId = data.readInt32();
		size_t size = data.readInt32();
		sp<IMemoryHeap> Data = requestBuffer(bufferId, size);
		if (Data != NULL)
		{
			reply->writeStrongBinder(Data->asBinder());
		}
		return NO_ERROR;
		break;
	}
	case RELEASEBUFFER:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		int bufferId = data.readInt32();
		int ret = releaseBuffer(bufferId);
		reply->writeInt32(ret);
		return NO_ERROR;
		break;
	}
	case CONFIG_LOAD_FIRMWARE:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		int ret = configLoadFirmware();
		reply->writeInt32(ret);
		return NO_ERROR;
		break;
	}
	case CONFIG_UNLOAD_FIRMWARE:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		configUnLoadFirmware();
		return NO_ERROR;
		break;
	}
	case CONFIG_START_FIRMWARE:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		configStartFirmware();
		return NO_ERROR;
		break;
	}
	case CONFIG_DONE_FIRMWARE:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		configDoneFirmware();
		return NO_ERROR;
		break;
	}
	case CONFIG_ABORT_FIRMWARE:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		configAbortFirmware();
		return NO_ERROR;
		break;
	}
	case CONFIG_SETARG_FIRMWARE:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		int arg_ID = data.readInt32();
		void* arg = (void*)data.readInt32();
		size_t size = data.readInt32();

		// call set arg
		LOGD("[%s] arg_ID(%d), arg(%x), size(%d)", __FUNCTION__, arg_ID, (int)arg, (int) size);
		int ret = configSetArgFirmware(arg_ID, arg, size);
		reply->writeInt32(ret);
		return NO_ERROR;
		break;
	}

	case CONFIG_SETARG_FIRMWARE_SHAREDBUFFER:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		int arg_ID = data.readInt32();
		int bufferId = data.readInt32();
		size_t size = data.readInt32();

		// call set arg
		LOGD("[%s] arg_ID(%d), bufferId(%x), size(%d)", __FUNCTION__, arg_ID, (int)bufferId, (int) size);
		int ret = configSetArgSharedBufferFirmware(arg_ID, bufferId, size);
		reply->writeInt32(ret);
		return NO_ERROR;
		break;
	}

	case CONFIG_DESTABILIZE_ARG_FIRMWARE:
	{
		CHECK_INTERFACE(IOlaBuffer, data, reply);
		int arg_ID = data.readInt32();

		// call set arg
		LOGD("[%s] arg_ID(%d)", __FUNCTION__, arg_ID);
		int ret = configDestabilizeArgFirmware(arg_ID);
		reply->writeInt32(ret);
		return NO_ERROR;
		break;
	}

	default:
		return BBinder::onTransact(code, data, reply, flags);
	}
}

}; // namespace android
