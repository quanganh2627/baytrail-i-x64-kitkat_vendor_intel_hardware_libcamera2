#ifndef GLUE_H
#define GLUE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#define int32 int32_t
#define int16 int16_t
#define int8  int8_t
#define uint32 uint32_t
#define uint16 uint16_t
#define uint8  uint8_t

#define OSCL_NEW(T,params) new T params
#define OSCL_STATIC_CAST(type,exp) ((type)(exp))
#define OSCL_ASSERT(expr) assert(expr)
#define OSCL_ARRAY_DELETE(ptr) delete [] ptr
#define OSCL_ARRAY_NEW(T,count) new T[count]
#define OSCL_FIRST_CATCH_ANY(leave_status, statements) \
	if (leave_status!= 0) {statements;}
#define OSCL_TRY(leave_status, statements) \
	statements
#define OSCL_EXPORT_REF
#define OSCL_IMPORT_REF
#define OSCL_UNUSED_ARG(x)

#define oscl_memcpy memcpy
#define oscl_memset memset
#define oscl_free free
#define oscl_malloc malloc

#endif
