#include "Jpeg_p.h"
#include <stdio.h>
#include <jpeglib.h>
#include <jerror.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* libjpeg longjmp remains inside C; no C++ destructor is bypassed. */
struct context {
    struct jpeg_error_mgr error;
    jmp_buf jump;
    struct ii_jpeg_result result;
    struct jpeg_decompress_struct decoder;
    struct jpeg_compress_struct encoder;
    struct jpeg_destination_mgr destination;
    unsigned char chunk[4096];
    size_t maximum;
};
static void failed(j_common_ptr c) {
    struct context *s=(struct context *)c->err;
    (*c->err->format_message)(c,s->result.error);
    longjmp(s->jump,1);
}
static void warning(j_common_ptr c,int level) { if (level<0) failed(c); }
static struct ii_jpeg_result finish(struct context *s,int encoding) {
    struct ii_jpeg_result result=s->result;
    if (encoding) jpeg_destroy_compress(&s->encoder); else jpeg_destroy_decompress(&s->decoder);
    free(s); return result;
}
struct ii_jpeg_result ii_jpeg_decode(const unsigned char *bytes,size_t size,unsigned dimension,size_t maximum,int info_only) {
    struct context *s=(struct context *)calloc(1,sizeof(*s));
    if (!s) { struct ii_jpeg_result r={0}; r.limit=1; strcpy(r.error,"allocation failed"); return r; }
    s->decoder.err=jpeg_std_error(&s->error); s->error.error_exit=failed; s->error.emit_message=warning;
    if (setjmp(s->jump)) { free(s->result.bytes); s->result.bytes=NULL; return finish(s,0); }
    jpeg_create_decompress(&s->decoder);
    if (size>ULONG_MAX) { s->result.limit=1; ERREXIT(&s->decoder,JERR_INPUT_EOF); }
    jpeg_mem_src(&s->decoder,bytes,(unsigned long)size); jpeg_read_header(&s->decoder,TRUE);
    s->result.width=s->decoder.image_width; s->result.height=s->decoder.image_height;
    if (!s->result.width || !s->result.height || s->result.width>dimension || s->result.height>dimension ||
        s->result.width>maximum/4/s->result.height) {
        s->result.limit=1; ERREXIT(&s->decoder,JERR_WIDTH_OVERFLOW);
    }
    if (info_only) return finish(s,0);
    if (s->decoder.jpeg_color_space==JCS_CMYK || s->decoder.jpeg_color_space==JCS_YCCK)
        ERREXIT(&s->decoder,JERR_CONVERSION_NOTIMPL);
    s->decoder.out_color_space=JCS_RGB; jpeg_start_decompress(&s->decoder);
    s->result.size=(size_t)s->result.width*s->result.height*4;
    s->result.bytes=(unsigned char *)malloc(s->result.size);
    if (!s->result.bytes) { s->result.limit=1; ERREXIT1(&s->decoder,JERR_OUT_OF_MEMORY,0); }
    JSAMPARRAY row=(*s->decoder.mem->alloc_sarray)((j_common_ptr)&s->decoder,JPOOL_IMAGE,s->result.width*3,1);
    while (s->decoder.output_scanline<s->decoder.output_height) {
        size_t y=s->decoder.output_scanline; jpeg_read_scanlines(&s->decoder,row,1);
        for (unsigned x=0;x<s->result.width;++x) {
            unsigned char *pixel=s->result.bytes+(y*s->result.width+x)*4;
            memcpy(pixel,row[0]+x*3,3); pixel[3]=255;
        }
    }
    jpeg_finish_decompress(&s->decoder); return finish(s,0);
}
static void destination_init(j_compress_ptr c) {
    struct context *s=(struct context *)c->err;
    c->dest->next_output_byte=s->chunk; c->dest->free_in_buffer=sizeof(s->chunk);
}
static void append(j_compress_ptr c,size_t count) {
    struct context *s=(struct context *)c->err;
    if (count>s->maximum-s->result.size) { s->result.limit=1; ERREXIT1(c,JERR_OUT_OF_MEMORY,0); }
    unsigned char *next=(unsigned char *)realloc(s->result.bytes,s->result.size+count);
    if (!next) { s->result.limit=1; ERREXIT1(c,JERR_OUT_OF_MEMORY,0); }
    s->result.bytes=next; memcpy(next+s->result.size,s->chunk,count); s->result.size+=count;
}
static boolean destination_full(j_compress_ptr c) { append(c,4096); destination_init(c); return TRUE; }
static void destination_finish(j_compress_ptr c) { append(c,4096-c->dest->free_in_buffer); }
struct ii_jpeg_result ii_jpeg_encode(const unsigned char *bytes,unsigned width,unsigned height,size_t stride,int quality,size_t maximum) {
    struct context *s=(struct context *)calloc(1,sizeof(*s));
    if (!s) { struct ii_jpeg_result r={0}; r.limit=1; strcpy(r.error,"allocation failed"); return r; }
    s->maximum=maximum;
    s->encoder.err=jpeg_std_error(&s->error); s->error.error_exit=failed; s->error.emit_message=warning;
    if (setjmp(s->jump)) { free(s->result.bytes); s->result.bytes=NULL; return finish(s,1); }
    jpeg_create_compress(&s->encoder);
    s->destination.init_destination=destination_init; s->destination.empty_output_buffer=destination_full;
    s->destination.term_destination=destination_finish; s->encoder.dest=&s->destination;
    s->encoder.image_width=width; s->encoder.image_height=height; s->encoder.input_components=3;
    s->encoder.in_color_space=JCS_RGB;
    jpeg_set_defaults(&s->encoder); jpeg_set_quality(&s->encoder,quality,TRUE); jpeg_start_compress(&s->encoder,TRUE);
    while (s->encoder.next_scanline<s->encoder.image_height) {
        JSAMPROW row=(JSAMPROW)(bytes+(size_t)s->encoder.next_scanline*stride); jpeg_write_scanlines(&s->encoder,&row,1);
    }
    jpeg_finish_compress(&s->encoder); return finish(s,1);
}
