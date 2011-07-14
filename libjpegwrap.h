#ifndef LIBJPEGWRAP__H__
#define LIBJPEGWRAP__H__
#include <va/va.h>
#include <sys/time.h>
#include <cutils/log.h>
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "fcntl.h"
#include "jpeglib.h"
#include "jpeglib_ext.h"

#ifdef __cplusplus
}
#endif

/*jpeg destination manager struct*/
typedef struct {
    struct jpeg_destination_mgr pub; /* public fields */
    JSAMPLE * encodeblock;    /* encode block buffer */
    JSAMPLE *outjpegbuf; /* JPEG output buffer*/
    JSAMPLE *outjpegbufpos; /* JPEG output buffer current ptr*/
    int outjpegbufsize; /*JPEG output buffer size*/
    int *datacount; /* JPEG output buffer data written count*/
} jpeg_destmgr,*jpeg_destmgr_ptr;

class HWLibjpegWrap {
private:
    struct jpeg_compress_struct mCinfo; /*jpeg compress info*/
    struct jpeg_error_mgr mJerr; /* error manager */
    jpeg_destmgr mDest; /* jpeg encode destination manager*/
    int mJpegsize; /* jpeg encode size */
    void* mUsrptr; /* buffer share user ptr */
    int mJpegQuality; /*jpeg quality*/

//    void init_destination(j_compress_ptr cinfo);
//    boolean empty_output_buffer(j_compress_ptr cinfo);
//    void term_destination(j_compress_ptr cinfo);
    int setup_jpeg_destmgr(j_compress_ptr cinfo, JSAMPLE *jpegbuf, int jpegbuf_size);

public:
    HWLibjpegWrap(); /* constructor */
    ~HWLibjpegWrap(); /* destructor */
    static const int    default_block_size = 4096; /* default encode block size */
    static const int    default_jpeg_quality =  75; /*defualt jpeg encode quality*/
    int initHwBufferShare(JSAMPLE *jpegbuf, int jpegbuf_size,int width,int height,void** usrptr);
    int startJPEGEncodebyHwBufferShare();

    /*property set/get functions*/
    int getJpeqQuality();
    int getJpegSize();
    void setJpeginfo(int width,int height,int inputcomponent,J_COLOR_SPACE colorspace,int quality);

    void savetofile(JSAMPLE *jpegbuf,int jpegbuf_size,char* filename); /*helper function*/
};
#endif
