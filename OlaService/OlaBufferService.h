#ifndef _OLABUFFERSERICE_H_
#define _OLABUFFERSERICE_H_

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

#include "IOlaBuffer.h"

#if 0
#define configLoadFirmware	Ola_BufferService_ConfigLoadFirmware
#define configUnloadFirmware	Ola_BufferService_ConfigUnLoadFirmware
#define configStartFirmware	Ola_BufferService_ConfigStartFirmware
#define configDoneFirmware	Ola_BufferService_ConfigDoneFirmware
#define configAbortFirmware	Ola_BufferService_ConfigAbortFirmware
#define configSetArgFirmware	Ola_BufferService_ConfigSetArgFirmware
#define configDestabilizeArgFirmware \
				Ola_BufferService_ConfigDestabilizeArgFirmware
#endif
#ifndef UINT16_MAX
#define UINT16_MAX          0xffff
#endif

#define N_OLABUFFERS 16
#define OLABUFFER_MEMORY_SIZE (((1280*960)+256)*3/2)  /* sizeof  shared memory*/

typedef enum {
    FW_arg_task_descr_ID = 0,
#ifdef FA_DESCR_RELATIVE
    FW_arg_func_descr_ID,
    FW_arg_data_descr_ID,
#endif
    FW_arg_data_alloc_ID,       /* F.F.S. separation of {i,o,io} pointers */
    FW_arg_frame_descr_ID,      /* When the FA takes it input directly from the preceding pipeline function */
    N_FW_arg_ID,
    L_FW_arg_ID = UINT16_MAX /* Make the enum 16-bit */
} FW_arg_ID_t;

#ifdef __cplusplus
extern "C" {
#endif

extern char*    Ola_BufferService_GetBufferMemPointer(void);
extern char*	Ola_BufferService_RequestBuffer(int bufferId, int size);
extern int	    Ola_BufferService_ReleaseBuffer(int bufferId);
extern int      Ola_BufferService_Initiate();
extern char*    Ola_BufferService_DeInitialize();

extern int  Ola_BufferService_ConfigLoadFirmware();
extern void Ola_BufferService_ConfigUnLoadFirmware();
extern int  Ola_BufferService_ConfigStartFirmware();
extern int  Ola_BufferService_ConfigDoneFirmware();
extern void Ola_BufferService_ConfigAbortFirmware();
extern int  Ola_BufferService_ConfigSetArgFirmware(const int arg_ID, const void *arg, const size_t size);
extern int  Ola_BufferService_ConfigDestabilizeArgFirmware(const int arg_ID);
extern int  Ola_BufferService_ConfigSetArgSharedBufferFirmware(const int arg_ID, int bufferId, const size_t size);

#ifdef __cplusplus
}
#endif

extern android::HalProxyOla* gHAL;
namespace android {

// ---------------------------------------- server side
class OlaBufferService : public BnOlaBuffer{
public:
	static void instantiate();
	OlaBufferService();
	virtual ~OlaBufferService();

    virtual sp<IMemoryHeap> getPreviewBuffer();
    virtual int	            releaseBuffer(int bufferId);
	virtual sp<IMemoryHeap>	requestBuffer(int bufferId, size_t size);
	virtual int             configLoadFirmware(void);
	virtual void            configUnLoadFirmware(void);
	virtual int             configStartFirmware(void);
	virtual void            configDoneFirmware(void);
	virtual void            configAbortFirmware(void);
	virtual int             configSetArgFirmware(const int  arg_ID,
			                                     const void      *arg,
			                                     const size_t    size);
	virtual int           configDestabilizeArgFirmware(const int arg_ID);
	virtual int           configSetArgSharedBufferFirmware(const int arg_ID,
	                                                             int bufferId,
	                                                       const size_t  size);



private:
	sp<MemoryHeapBase> mMemHeapPreview;
	sp<IMemoryHeap> mMemHeapAccel[N_OLABUFFERS];
};

} //namespace android
#endif // _OLABUFFERSERICE_H_
