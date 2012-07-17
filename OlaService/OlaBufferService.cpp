
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

#include "OlaService/IOlaBuffer.h"
#include <binder/MemoryHeapBase.h>
#include <binder/IServiceManager.h>
#include <binder/IPCThreadState.h>

#include "HalProxyOla.h"
#include "OlaBufferService.h"


android::HalProxyOla* gHAL = NULL;

namespace android {

static sp<IOlaBuffer>   _getOlaBufferService();

// following 3 functions would be called from onTransact
sp<IMemoryHeap> OlaBufferService::getPreviewBuffer()
{
	LOGI("[%s] OlaBufferService called", __FUNCTION__);
	return mMemHeapPreview;
}

int OlaBufferService::releaseBuffer(int bufferId)
{
	// TODO: implement
	// "MemoryHeapBase" objects are reference counted by the system and auto deleted
	LOGI("[%s] OlaBufferService called", __FUNCTION__);
	mMemHeapAccel[bufferId] = 0;
	return 0;
}

sp<IMemoryHeap> OlaBufferService::requestBuffer(int bufferId, size_t size)
{
	LOGI("[%s] server called bufferId(%d), size(%d) ", __FUNCTION__, bufferId, size);
	if( mMemHeapAccel[bufferId] == NULL ) {
		mMemHeapAccel[bufferId] = new MemoryHeapBase(size);
		LOGI("[%s] NEW memoryHeapBase for bufferId(%d), size(%d) ", __FUNCTION__, bufferId, size);

		memset( mMemHeapAccel[bufferId]->getBase(), 0, size);
	}

	return mMemHeapAccel[bufferId];
}


int OlaBufferService::configLoadFirmware()
{
	if( gHAL != NULL ) {
		LOGI("[%s] configLoadFirmware called in server side", __FUNCTION__);
		return gHAL->configLoadFirmware();
	}
	return -100;
}

void OlaBufferService::configUnLoadFirmware()
{
	if( gHAL != NULL ) {
		LOGI("[%s] configUnLoadFirmware called in server side", __FUNCTION__);
		gHAL->configUnloadFirmware();
	}
}

int OlaBufferService::configStartFirmware()
{
	if( gHAL != NULL ) {
		LOGI("[%s] configStartFirmware called in server side", __FUNCTION__);
		return 0;//gHAL->configStartFirmware();
	}
	return -100;
}

void OlaBufferService::configDoneFirmware()
{
	if( gHAL != NULL ) {
		LOGI("[%s] configDoneFirmware called in server side", __FUNCTION__);
		//gHAL->configDoneFirmware();
	}
}

void OlaBufferService::configAbortFirmware()
{
	if( gHAL != NULL ) {
		LOGI("[%s] configAbortFirmware called in server side", __FUNCTION__);
		//gHAL->configAbortFirmware();
	}
}

int OlaBufferService::configSetArgFirmware(const int arg_ID, const void *arg, const size_t size)
{
	if( gHAL != NULL ) {
		LOGI("[SERVER::%s] configSetArgFirmware called with arg_ID (%d)", __FUNCTION__, arg_ID);
		return 0;//gHAL->configSetArgFirmware( arg_ID, arg, size);
	}
	return -100;
}

int OlaBufferService::configDestabilizeArgFirmware(const int arg_ID)
{
	if( gHAL != NULL ) {
		LOGI("[SERVER::%s] configSetArgFirmware called with arg_ID (%d)", __FUNCTION__, arg_ID);
		return 0;//gHAL->configDestabilizeArgFirmware( arg_ID);
	}
	return -100;
}

int OlaBufferService::configSetArgSharedBufferFirmware(const int arg_ID, int bufferId, const size_t size)
{
	if( gHAL != NULL ) {
		LOGI("[SERVER::%s] called with bufferId(%d) arg_ID(%d)", __FUNCTION__, bufferId, arg_ID);

		void* arg = Ola_BufferService_RequestBuffer(bufferId ,size);
		LOGW("[SERVER::%s] content bufferId[%d]: int* [%d]:%x, [%d]:%x, [%d]:%x, [%d]:%x", __FUNCTION__, bufferId, 0, ((int*)arg)[0], 1, ((int*)arg)[1], 2, ((int*)arg)[2], 3, ((int*)arg)[3]);
		return 0;//gHAL->configSetArgFirmware( arg_ID, arg, size);
	}
	return -100;
}

// static method
void OlaBufferService::instantiate()
{
    static int g_isInitialized = 0;
    status_t status;
    if (g_isInitialized == 0) {
        LOGD("Initializing com.olaworks.olabuffer");
        status = defaultServiceManager()->addService(String16("com.olaworks.olabuffer"), new OlaBufferService());
        g_isInitialized = 1;
    } else {
        LOGD("Already initialised. com.olaworks.olabuffer");
    }
}

OlaBufferService::OlaBufferService()
{
	//The memory is allocated using a MemoryHeapBase, and thereby is using ashmem
	mMemHeapPreview = new MemoryHeapBase(OLABUFFER_MEMORY_SIZE);

	//Initiate first value in buffer for test
	unsigned int *base = (unsigned int *) mMemHeapPreview->getBase();
	*base=0;
	LOGI("[%s] Constructor %d this 0x%x", __FUNCTION__, *base, (unsigned int)this);
}

OlaBufferService::~OlaBufferService()
{
    LOGI("[%s] OlaBufferService DESTRUCTOR", __FUNCTION__);

	mMemHeapPreview = 0;
	for( int i=0 ;i< N_OLABUFFERS; i++) {
		mMemHeapAccel[i] = 0;
	}
}


// ---------------------------------------- client side
// -------------- made by C Interfaces for CameraHardWare.cpp & libOla.so

// must be static global
static sp<IMemoryHeap> receiverMemBasePreview;
static sp<IMemoryHeap> receiverMemBaseAcc[N_OLABUFFERS];



static sp<IOlaBuffer>	_getOlaBufferService()
{
	sp<IOlaBuffer> olaBuffer;
//	LOGI("[%s] called", __FUNCTION__);
	sp<IServiceManager> sm = defaultServiceManager();
	sp<IBinder> binder;
	binder = sm->getService(String16(OLABUFFER_DESCRIPTOR));
	if (binder != 0)
	{
		olaBuffer = IOlaBuffer::asInterface(binder);
	}
	return olaBuffer;
}


extern "C" char* Ola_BufferService_GetBufferMemPointer(void)
{
	int ret_ptr;
	int pageSize;
	LOGI("[%s] called", __FUNCTION__);
	pageSize = getpagesize();

	if( receiverMemBasePreview == NULL) {

		sp<IOlaBuffer> olaBuffer;
		/* Get the buffer service */
		olaBuffer = _getOlaBufferService();
		if (olaBuffer == NULL)
		{
			LOGE("The buffer service is not published");
			return (char *)-1; /* return an errorcode... */
		}
		receiverMemBasePreview = olaBuffer->getPreviewBuffer();
	}

	if( receiverMemBasePreview == NULL ) return NULL;

	//return (char*) receiverMemBasePreview->getBase();
	ret_ptr = (((int)receiverMemBasePreview->getBase()) + 2*pageSize) & ~(pageSize-1) ;
	LOG1("@%s, got %p",__FUNCTION__, (void*)ret_ptr);
	return (char*)ret_ptr;
}

extern "C" int Ola_BufferService_ReleaseBuffer(int bufferId)
{
	LOGI("[%s] called", __FUNCTION__);
	int ret;
	sp<IOlaBuffer> olaBuffer;
	olaBuffer = _getOlaBufferService();
	if (olaBuffer == NULL)
	{
		LOGE("The buffer service is not published");
		return -1; /* return an errorcode... */
	}
	receiverMemBaseAcc[bufferId] = 0;
	return olaBuffer->releaseBuffer(bufferId);
}

extern "C" char* Ola_BufferService_RequestBuffer(int bufferId, size_t size)
{
	unsigned int ret_ptr;
	int pageSize;
	LOGI("[%s] called, bufferId(%d), size(%d)", __FUNCTION__, bufferId, size);
	pageSize = getpagesize();
	sp<IOlaBuffer> olaBuffer;
	olaBuffer = _getOlaBufferService();
	//int aligned_size = DDR_BYTES_PER_WORD*((size+DDR_BYTES_PER_WORD-1)/DDR_BYTES_PER_WORD);
	int aligned_size = size+ 2*pageSize;
	if (olaBuffer == NULL)
	{
		LOGE("The buffer service is not published");
		return (char *)-1; /* return an errorcode... */
	}
	receiverMemBaseAcc[bufferId] = olaBuffer->requestBuffer( bufferId, aligned_size );
	if( receiverMemBaseAcc[bufferId] == NULL) {
		return NULL;
	}

	ret_ptr = (   ((unsigned int)receiverMemBaseAcc[bufferId]->getBase())   + 2*pageSize) & ~(pageSize-1) ;
	LOGD("[%s] %x -> %x",__FUNCTION__, (int)receiverMemBaseAcc[bufferId]->getBase(), ret_ptr);

	LOGI("[%s] return mem base(%x) size(%d)", __FUNCTION__, ret_ptr, aligned_size);
	return (char*)ret_ptr;
}

extern "C" int Ola_BufferService_ConfigLoadFirmware()
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	return olaBuffer->configLoadFirmware();
}

extern "C" void Ola_BufferService_ConfigUnLoadFirmware()
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	olaBuffer->configUnLoadFirmware();
}

extern "C" int Ola_BufferService_ConfigStartFirmware()
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	return olaBuffer->configStartFirmware();
}

extern "C" void Ola_BufferService_ConfigDoneFirmware()
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	olaBuffer->configDoneFirmware();
}

extern "C" void Ola_BufferService_ConfigAbortFirmware()
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	olaBuffer->configAbortFirmware();
}

extern "C" int Ola_BufferService_ConfigSetArgFirmware(const int arg_ID, const void *arg, const size_t size)
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	LOGE("[CLIENT::%s] arg_Id(%d), arg(%x), size(%d)", __FUNCTION__, arg_ID, (unsigned int)arg, size);
	return olaBuffer->configSetArgFirmware(arg_ID, arg, size);
}

extern "C" int Ola_BufferService_ConfigDestabilizeArgFirmware(const int arg_ID)
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	LOGE("[CLIENT::%s] arg_Id(%d)", __FUNCTION__, arg_ID);
	return olaBuffer->configDestabilizeArgFirmware(arg_ID);
}


extern "C" int Ola_BufferService_ConfigSetArgSharedBufferFirmware(const int arg_ID, int bufferId, const size_t size)
{
	sp<IOlaBuffer> olaBuffer  = _getOlaBufferService();
	LOGE("[CLIENT::%s] arg_Id(%d), bufferId(%d), size(%d)", __FUNCTION__, arg_ID, bufferId, size);
	return olaBuffer->configSetArgSharedBufferFirmware(arg_ID, bufferId, size);
}

extern "C" void Ola_BufferService_DeInitialize()
{
    LOGD("[%s] Ola_BufferService_DeInitialize fn", __FUNCTION__);

	int i;
	receiverMemBasePreview = 0;
	for(i=0; i<N_OLABUFFERS; i++) {
		receiverMemBaseAcc[i] = 0;
	}
	gHAL = NULL;

}

extern "C" int Ola_BufferService_Initiate()
{
	OlaBufferService::instantiate();
	//Create binder threads for this "server"
	ProcessState::self()->startThreadPool();

	LOGI("[%s] initiate", __FUNCTION__);


	// wait for threads to stop
	//   IPCThreadState::self()->joinThreadPool();
	Ola_BufferService_DeInitialize();

    LOGD("[%s] Ola_BufferService_DeInitialize completed", __FUNCTION__);

	return 0;
}



} // namespace android
