#include "libjpegwrap.h"

//#define HWLIBJPEG_TIME_MEASURE
#ifdef HWLIBJPEG_TIME_MEASURE
static struct timeval hw_start,hw_end;
#endif

/*
Function: HWLibjpegWrap
Description:
   constructor to initialize member of HWLibjpegWrap
Param:
  none
return:
  none
*/
HWLibjpegWrap::HWLibjpegWrap() {
    memset(&mCinfo , 0, sizeof(mCinfo));
    memset(&mJerr , 0, sizeof(mJerr));
    memset(&mDest , 0, sizeof(mDest));
    mJpegsize = 0;
    mUsrptr = NULL;
    mJpegQuality = 0;
}

/*
Function: HWLibjpegWrap
Description:
   destructor to release resource of HWLibjpegWrap
Param:
  none
return:
  none
*/
HWLibjpegWrap::~HWLibjpegWrap() {

}

/*
Function: initHwBufferShare
Description: initialzie jpeg encode with buffer share enable by hwlibjpeg
Param:
    jpegbuf - jpeg encode output buffer
    jpegbuf_size - size of jpegbuf
    width - jpeg picture width
    height - jpeg picture height
    usrptr - user pointer for share buffer as output
Return:
   0 - successfully
   -1 - fail
*/
int HWLibjpegWrap::initHwBufferShare(JSAMPLE *jpegbuf, int jpegbuf_size,int width,int height,void** usrptr) {
    if(NULL == jpegbuf || jpegbuf_size <= 0 || width <=0 || height <=0 || NULL == usrptr) {
        LOGE("%s - parameter error !",__func__);
        return -1;
    }
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_start, 0);
#endif
    //initialize hw libjpeg
    mUsrptr = NULL;
    mJpegsize = 0;
    memset(&mCinfo , 0, sizeof(mCinfo));
    mCinfo.err = jpeg_std_error(&mJerr);
    jpeg_create_compress(&mCinfo);
    //LOGD("%s - jpeg_create_compress done !",__func__);
    if(setup_jpeg_destmgr(&mCinfo, jpegbuf,jpegbuf_size)<0) {
        LOGE("%s- setup_jpeg_destmgr fail",__func__);
        jpeg_destroy_compress(&mCinfo);
        return -1;
    }
    if(!jpeg_get_userptr_from_surface(&mCinfo,width,height,VA_FOURCC_NV12,(char**)usrptr)) {
        LOGE("%s- jpeg_get_new_userptr_for_surface_buffer fail",__func__);
        jpeg_destroy_compress(&mCinfo);
        return -1;
    }
    //LOGD("%s- jpeg_get_new_userptr_for_surface_buffer done !",__func__);
    if(NULL == *usrptr) {
        LOGD("%s- NULL == *usrptr",__func__);
        jpeg_destroy_compress(&mCinfo);
        return -1;
    }
    mUsrptr = *usrptr;
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_end, 0);
    LOGD("%s time - %d ms",__func__,(hw_end.tv_sec-hw_start.tv_sec)*1000+(hw_end.tv_usec-hw_start.tv_usec)/1000);
#endif

    return 0;
}

/*
Function: startJPEGEncodebyHwBufferShare
Description: start jpeg encode with buffer share enable by hwlibjpeg, initHwBufferShare should be
called & related property should be set before it.
Param:
   none
Return:
   0 - successfully
   -1 - fail
*/
int HWLibjpegWrap::startJPEGEncodebyHwBufferShare() {
    if(NULL == mUsrptr)// initHwBufferShare not successfully , return error
        return -1;
    jpeg_set_defaults(&mCinfo);
    jpeg_set_colorspace(&mCinfo,JCS_YCbCr);
    jpeg_set_quality(&mCinfo,mJpegQuality,TRUE); //set encode quality 0~100
    mCinfo.raw_data_in = TRUE;
    mCinfo.dct_method = JDCT_FLOAT;
#ifdef HWLIBJPEG_TIME_MEASURE
    LOGD("%s- jpeg parameters setting done !",__func__);
    gettimeofday(&hw_start, 0);
#endif
    jpeg_start_compress(&mCinfo, TRUE );
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_end, 0);
    LOGD("%s- jpeg_start_compress done !",__func__);
    LOGD("jpeg_start_compress time - %d ms",(hw_end.tv_sec-hw_start.tv_sec)*1000+(hw_end.tv_usec-hw_start.tv_usec)/1000);
#endif

#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_start, 0);
#endif
    jpeg_finish_compress(&mCinfo);
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_end, 0);
    LOGD("%s- jpeg_finish_compress done !",__func__);
    LOGD("jpeg_finish_compress time - %d ms",(hw_end.tv_sec-hw_start.tv_sec)*1000+(hw_end.tv_usec-hw_start.tv_usec)/1000);
#endif

#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_start, 0);
#endif
    jpeg_destroy_compress(&mCinfo);
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_end, 0);
    LOGD("%s- jpeg_destroy_compress done !",__func__);
    LOGD("jpeg_destroy_compress times - %d ms",(hw_end.tv_sec-hw_start.tv_sec)*1000+(hw_end.tv_usec-hw_start.tv_usec)/1000);
#endif
    if( mJpegsize > 0)
        return 0;
    else
        return -1;
}

/*
Function: getJpeqQuality
Description:
   get jpeg encode quality
Param:
  none
return:
  jpeg encode quality value
*/
int HWLibjpegWrap::getJpeqQuality() {
    return mJpegQuality;
}

/*
Function: setJpeginfo
Description:
   set jpeg encode related information
Param:
  width - picture width
  heitht - picture height
  inputcomponent - input color componect
  colorspace - input color space
  quality - jpeg encode quality ( 0~100 )
return:
  none
*/
void HWLibjpegWrap::setJpeginfo(int width,int height,int inputcomponent,J_COLOR_SPACE colorspace,int quality){
    mCinfo.image_width = width;
    mCinfo.image_height = height;
    mCinfo.input_components = inputcomponent;
    mCinfo.in_color_space = colorspace;
    if(quality >= 0 && quality <=100)
        mJpegQuality = quality;
    else
        mJpegQuality = HWLibjpegWrap::default_jpeg_quality;

}

/*
Function: getJpegSize
Description:
   return jpeg encode result size
Param:
   none
return:
  mJpegsize
*/
int HWLibjpegWrap::getJpegSize() {
    return mJpegsize;
}

/*
Function: savetofile
Description: helper function - save jpeg data to file in disk
Param:
    jpegbuf - jpeg data buf
    jpegbuf_size - jpeg data size
    filename - file name in disk
*/
void HWLibjpegWrap::savetofile(JSAMPLE *jpegbuf,int jpegbuf_size,char* filename)
{
    const char *name = filename;
    unlink(name);
    int tfd = creat(name, 0666);
    if (write (tfd, jpegbuf, jpegbuf_size ) != jpegbuf_size ) {
        LOGD("Failt to write jpeg file into disk");
    }
    else
        LOGD("Suceess to write jpeg file into disk");

    close(tfd);
}

/*
Function: init_destination
Description:
  initialize to allocate encode block and other members in jpeg_destmgr
Param:
  cinfo - compress object ptr
return:
  none
*/
void init_destination(j_compress_ptr cinfo) {
    jpeg_destmgr_ptr dest = (jpeg_destmgr_ptr) cinfo->dest;
    /* Allocate the encode block buffer*/
    dest->encodeblock = (JSAMPLE *)(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_IMAGE, HWLibjpegWrap::default_block_size * sizeof(JSAMPLE));
    *(dest->datacount) = 0;/* there is no data in JPEG output buffer now*/
    dest->pub.next_output_byte = dest->encodeblock;/*set next JPEG encode data output */
    dest->pub.free_in_buffer = HWLibjpegWrap::default_block_size;/*set size of next JPEG encode data output */
}

/*
Function: empty_output_buffer
Description.:
  called whenever jpeg encode block fills up, then copy jpeg data to output buffer
Param:
  cinfo - compress object ptr
return:
  none
*/
boolean empty_output_buffer(j_compress_ptr cinfo)
{
    jpeg_destmgr_ptr dest = (jpeg_destmgr_ptr) cinfo->dest;
    dest->outjpegbufsize -= HWLibjpegWrap::default_block_size;
    if(dest->outjpegbufsize < 0)/*buffer overflow*/
    {
        LOGE("******empty_output_buffer buffer overflow**********");
        *(dest->datacount) = 0;
        return FALSE;
    }
    memcpy(dest->outjpegbufpos, dest->encodeblock, HWLibjpegWrap::default_block_size);/*copy JPEG data from encode block to output*/
    dest->outjpegbufpos += HWLibjpegWrap::default_block_size;
    *(dest->datacount) += HWLibjpegWrap::default_block_size;
    dest->pub.next_output_byte = dest->encodeblock;
    dest->pub.free_in_buffer = HWLibjpegWrap::default_block_size;
    return TRUE;
}

/*
Function: term_destination
Description.:
  called when jpeg encode finished , then write any data remaining in the block to output buffer
Param:
  cinfo - compress object ptr
return:
  none
*/
void term_destination(j_compress_ptr cinfo)
{
    jpeg_destmgr_ptr dest = (jpeg_destmgr_ptr) cinfo->dest;
    size_t datacount = HWLibjpegWrap::default_block_size - dest->pub.free_in_buffer;
    dest->outjpegbufsize -= datacount;
    if(dest->outjpegbufsize < 0)/*buffer overflow*/
    {
        *(dest->datacount) = 0;
        return ;
    }
    /* flush data remaining in the encode block to output jpeg buffer */
    memcpy(dest->outjpegbufpos, dest->encodeblock, datacount);
    dest->outjpegbufpos += datacount;
    *(dest->datacount) += datacount;
}

/*
Function: setup_jpeg_destmgr
Description.:
  set jpeg_destmgr members
Param:
  cinfo - compress object ptr
  jpegbuf - jpeg output buffer
  jpegbuf_size - jpeg output buffer size
return:
  0 - successfully
  -1 - fail
*/
int HWLibjpegWrap::setup_jpeg_destmgr(j_compress_ptr cinfo, JSAMPLE *jpegbuf, int jpegbuf_size)
{
    jpeg_destmgr_ptr dest;

    if(NULL == jpegbuf || jpegbuf_size <= 0 )
        return -1;
    if (cinfo->dest == NULL) {
        cinfo->dest = (struct jpeg_destination_mgr *)(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, sizeof(jpeg_destmgr));
    }

    dest = (jpeg_destmgr_ptr) cinfo->dest;
    dest->pub.init_destination = init_destination;
    dest->pub.empty_output_buffer = empty_output_buffer;
    dest->pub.term_destination = term_destination;
    dest->outjpegbuf= jpegbuf;
    dest->outjpegbufsize= jpegbuf_size;
    dest->outjpegbufpos= jpegbuf;
    dest->datacount = &mJpegsize;
    return 0;
}
