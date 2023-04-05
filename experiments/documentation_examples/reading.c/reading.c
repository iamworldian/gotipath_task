#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/file.h>

struct buffer_data {
    uint8_t *ptr;
    size_t size; 
};

static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

static int read_packet(void *opaque, uint8_t *buf, int buf_size)
{
    struct buffer_data *bd = (struct buffer_data *)opaque;
    buf_size = FFMIN(buf_size, bd->size);
 
    if (!buf_size)
        return AVERROR_EOF;
    printf("ptr:%p size:%zu\n", bd->ptr, bd->size);
 
    /* copy internal buffer data to buf */
    memcpy(buf, bd->ptr, buf_size);
    bd->ptr  += buf_size;
    bd->size -= buf_size;
 
    return buf_size;
}

int main(int argc, char *argv[]) {
    
    char *input_filename = NULL;

    if(argc < 2){
        logging("Need to specify a file");
        return -1;
    }

    input_filename = argv[1];

    int ret = 0;
    uint8_t *buffer = NULL;
    size_t buffer_size = 4096;

    ret = av_file_map(input_filename, &buffer, &buffer_size, 0, NULL);

    logging("%d %u %zu",ret,*(buffer),buffer_size);
    if (ret < 0)
        goto end;

    struct buffer_data bd = { 0 };
    bd.ptr = buffer;
    bd.size = buffer_size;

    AVFormatContext *p_format_context = NULL;
    if(!(p_format_context = avformat_alloc_context())){
        logging("Error avformt_alloc_context()");
        ret = AVERROR(ENOMEM);
        goto end;
    }

    uint8_t *io_context_buffer = NULL;
    size_t io_context_buffer_size = 4096;
    AVIOContext *p_io_context = NULL;

    io_context_buffer = av_malloc(io_context_buffer_size);
    if(!io_context_buffer){
        ret = AVERROR(ENOMEM);
        goto end;
    }

    p_io_context = avio_alloc_context(io_context_buffer,io_context_buffer_size,0,&bd,&read_packet,NULL,NULL);

    if (!p_io_context) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    p_format_context->pb = p_io_context;


    ret = avformat_open_input(&p_format_context, NULL, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not open input\n");
        goto end;
    }
 
    ret = avformat_find_stream_info(p_format_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "Could not find stream information\n");
        goto end;
    }
 
    av_dump_format(p_format_context, 0, input_filename, 0);

    end:

    avformat_close_input(&p_format_context);
 
  
    if (p_io_context)
        av_freep(&p_io_context->buffer);
    avio_context_free(&p_io_context);
 
    av_file_unmap(buffer, buffer_size);
 
    if (ret < 0) {
        fprintf(stderr, "Error occurred: %s\n", av_err2str(ret));
        return 1;
    }
 
    return 0;

}