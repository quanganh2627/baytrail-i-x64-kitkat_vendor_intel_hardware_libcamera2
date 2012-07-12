#ifndef _IOLABUFFER_H_
#define _IOLABUFFER_H_

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

#include <utils/RefBase.h>
#include <binder/IInterface.h>
#include <binder/Parcel.h>
#include <binder/IMemory.h>
//#include <utils/Timers.h>

#define OLABUFFER_DESCRIPTOR	"com.olaworks.olabuffer"

namespace android {



class IOlaBuffer: public IInterface
{
	public:
		enum {
			GET_PREVIEWBUFFER = IBinder::FIRST_CALL_TRANSACTION,
			REQUESTBUFFER,
			RELEASEBUFFER,
			CONFIG_LOAD_FIRMWARE,
			CONFIG_UNLOAD_FIRMWARE,
			CONFIG_START_FIRMWARE,
			CONFIG_DONE_FIRMWARE,
			CONFIG_ABORT_FIRMWARE,
			CONFIG_SETARG_FIRMWARE,
			CONFIG_SETARG_FIRMWARE_SHAREDBUFFER,
			CONFIG_DESTABILIZE_ARG_FIRMWARE,
		};

		DECLARE_META_INTERFACE(OlaBuffer);
		virtual sp<IMemoryHeap>           getPreviewBuffer() = 0;
		virtual sp<IMemoryHeap>         requestBuffer(int bufferId, size_t size) = 0;
		virtual int			releaseBuffer(int bufferId) = 0;

		/*
		 * "cfg()" part of the interface
		 *
		 * This is a simple encapsulation around the CSS acceleration API to
		 * hide the FW handle and (pointer to) the binary.
		 *
		 * It also abstracts the input argument enumeration
		 */

		/* return non-zero on error */
		virtual int           configLoadFirmware(void) = 0;
		virtual void          configUnLoadFirmware(void) = 0;
		virtual int           configStartFirmware(void) = 0;
		virtual void          configDoneFirmware(void) = 0;
		virtual void          configAbortFirmware(void) = 0;
		virtual int           configSetArgFirmware(const int	arg_ID, const void	*arg, const size_t	size) = 0;
		virtual int           configSetArgSharedBufferFirmware(const int	arg_ID, int bufferId, const size_t	size) = 0;
		virtual int           configDestabilizeArgFirmware(const int	arg_ID) = 0;
};

// --------------------------------------------------

class BnOlaBuffer: public BnInterface<IOlaBuffer>
{
public:
        virtual status_t    onTransact( uint32_t code,
                        const Parcel& data,
                        Parcel* reply,
                        uint32_t flags = 0);
};

}; // namespace android

#endif /* _IOLABUFFER_H_ */
