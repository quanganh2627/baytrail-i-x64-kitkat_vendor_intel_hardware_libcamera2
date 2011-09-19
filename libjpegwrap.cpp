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
    mJpegQuality = 0;
    mFlagInit=false;
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
    if(mFlagInit){
        //initialization successfully , we need to release resource in destructor
        jpeg_destroy_compress(&mCinfo);
    }
}

/*
Function: initHwBufferShare
Description: initialzie jpeg encode with buffer share enable by hwlibjpeg
Param:
    jpegbuf - jpeg encode output buffer
    jpegbuf_size - size of jpegbuf
    width - jpeg picture width
    height - jpeg picture height
    usrptr - user pointer array for share buffer as output
    usrptr_size - user pointer array size
Return:
   0 - successfully
   -1 - fail
*/
int HWLibjpegWrap::initHwBufferShare(JSAMPLE *jpegbuf, int jpegbuf_size,int width,int height,void** usrptr,int usrptr_size) {
    if(NULL == jpegbuf || jpegbuf_size <= 0 || width <=0 || height <=0 || NULL == usrptr || usrptr_size <= 0) {
        LOGE("%s - parameter error !",__func__);
        return -1;
    }
    if(mFlagInit)//already inilialized, just return successfully
        return 0;
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_start, 0);
#endif
    //initialize hw libjpeg
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
    int index;
    for(index=0;index<usrptr_size;index++){
        if(NULL == usrptr[index]) {
            LOGD("%s- NULL == usrptr[%d]",__func__,index);
            jpeg_destroy_compress(&mCinfo);
            return -1;
        }
    }
    //LOGD("%s- jpeg_get_new_userptr_for_surface_buffer done !",__func__);
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_end, 0);
    LOGD("%s time - %d ms",__func__,(hw_end.tv_sec-hw_start.tv_sec)*1000+(hw_end.tv_usec-hw_start.tv_usec)/1000);
#endif
    mFlagInit=true;
    return 0;
}

/*
Function: preStartJPEGEncodebyHwBufferShare
Description: Do some preparation before start jpeg encode with buffer share enable by hwlibjpeg,
initHwBufferShare & setJpeginfo should be called & related property should be set before it.
It can not be called multi-times.
Param:
   none
Return:
   0 - successfully
   -1 - fail
*/
int HWLibjpegWrap::preStartJPEGEncodebyHwBufferShare(){
    if(!mFlagInit)// initHwBufferShare not successfully , return error
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
    mCinfo.comp_info[0].h_samp_factor =
    mCinfo.comp_info[0].v_samp_factor = 2;
    mCinfo.comp_info[1].h_samp_factor =
    mCinfo.comp_info[1].v_samp_factor =
    mCinfo.comp_info[2].h_samp_factor =
    mCinfo.comp_info[2].v_samp_factor = 1;
    jpeg_start_compress(&mCinfo, TRUE );
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_end, 0);
    LOGD("%s- jpeg_start_compress done !",__func__);
    LOGD("jpeg_start_compress time - %d ms",(hw_end.tv_sec-hw_start.tv_sec)*1000+(hw_end.tv_usec-hw_start.tv_usec)/1000);
#endif

    return 0;
}

/*
Function: startJPEGEncodebyHwBufferShare
Description: start jpeg encode with buffer share enable by hwlibjpeg, preStartJPEGEncodebyHwBufferShare should be
called before it. It can be called multi-times for different usrptr input.
Param:
   void* usrptr - nv12 data usr pointer
Return:
   0 - successfully
   -1 - fail
*/
int HWLibjpegWrap::startJPEGEncodebyHwBufferShare(void* usrptr) {
    if(NULL == usrptr || NULL == mCinfo.dest )
        return -1;
    if(mJpegsize > 0){
        //The standard libjpeg api call flow is : jpeg_start_compress -> jpeg_write_raw_data -> jpeg_finish_compress
        //In order to use buffer share of hwlibjpeg in burst mode, we break the standard api call flow to jpeg_write_raw_data -> jpeg_finish_compress,
        //so we have to move init_destination logic to clean output buffer for new jpeg data written to output buffer here
        //which should be called by jpeg_start_compress in standard flow.
        jpeg_destmgr_ptr dest;
        dest = (jpeg_destmgr_ptr) mCinfo.dest;
        dest->outjpegbufpos = dest->outjpegbuf;/*set JPEG encode data start position in output buffer*/
        dest->pub.next_output_byte = dest->encodeblock;/*set next JPEG encode data output */
        dest->pub.free_in_buffer = HWLibjpegWrap::default_block_size;/*set size of next JPEG encode data output */
        *(dest->datacount)=0;
        mJpegsize = 0;
        //LOGD("%s- output buffer clean done !",__func__);
    }
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_start, 0);
#endif
    jpeg_write_raw_data(&mCinfo,(JSAMPIMAGE)usrptr,mCinfo.image_height);
#ifdef HWLIBJPEG_TIME_MEASURE
    gettimeofday(&hw_end, 0);
    LOGD("%s- jpeg_write_raw_data done !",__func__);
    LOGD("jpeg_write_raw_data time - %d ms",(hw_end.tv_sec-hw_start.tv_sec)*1000+(hw_end.tv_usec-hw_start.tv_usec)/1000);
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
  initialize to allocate encode block and other members in jpeg_destmgr, it will be caled by jpeg_start_compress
Param:
  cinfo - compress object ptr
return:
  none
*/
void init_destination(j_compress_ptr cinfo) {
    jpeg_destmgr_ptr dest = (jpeg_destmgr_ptr) cinfo->dest;
    /* Allocate the encode block buffer*/
    //LOGD("libjpegwrap init_destination call cinfo->dest 0x%x",cinfo->dest );
    if(NULL == dest->encodeblock)
        dest->encodeblock = (JSAMPLE *)(*cinfo->mem->alloc_small) ((j_common_ptr) cinfo, JPOOL_PERMANENT, HWLibjpegWrap::default_block_size * sizeof(JSAMPLE));
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
    if((*(dest->datacount)+HWLibjpegWrap::default_block_size) > dest->outjpegbufsize)/*buffer overflow*/
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
    //LOGD("libjpegwrap term_destination call cinfo->dest " );
    size_t datacount = HWLibjpegWrap::default_block_size - dest->pub.free_in_buffer;
    if( (*(dest->datacount) + datacount ) > dest->outjpegbufsize )/*buffer overflow*/
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
        memset(cinfo->dest,0,sizeof(jpeg_destmgr));
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
