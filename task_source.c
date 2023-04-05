#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>

typedef struct StreamContext
{
    AVCodecContext *decode_context;
    AVCodecContext *encode_context;

    AVFrame *decode_frame;
} StreamContext;
static StreamContext *stream_context;

typedef struct FilteringContext
{
    AVFilterContext *buffersink_context;
    AVFilterContext *buffersrc_context;
    AVFilterGraph *filter_graph;

    AVPacket *encode_packet;
    AVFrame *filtered_frame;
} FilteringContext;
static FilteringContext *filter_context;
int thumbnail_frame,chosen_frame;
int res_h,res_w,bitrate;

AVFormatContext *output_format_context;

static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

void dump_stream_info(AVStream *stream)
{
    logging("Stream Info");
    logging("r_frame_rate : %d %d\n", stream->r_frame_rate.den, stream->r_frame_rate.num);
    logging("duration : %d\n", stream->duration);
    logging("nb_frames : %d\n", stream->nb_frames);
}
void dump_codec_info(AVCodec *codec)
{
    logging("Codec Info\n");
    logging("name : %s\n", codec->name);
    logging("long_name : %s\n", codec->long_name);
}

static int init_filter(FilteringContext* fctx, AVCodecContext *dec_ctx,
        AVCodecContext *enc_ctx, const char *filter_spec)
{
    char args[512];
    int ret = 0;
    const AVFilter *buffersrc = NULL;
    const AVFilter *buffersink = NULL;
    AVFilterContext *buffersrc_ctx = NULL;
    AVFilterContext *buffersink_ctx = NULL;
    AVFilterInOut *outputs = avfilter_inout_alloc();
    AVFilterInOut *inputs  = avfilter_inout_alloc();
    AVFilterGraph *filter_graph = avfilter_graph_alloc();
 
    if (!outputs || !inputs || !filter_graph) {
        ret = AVERROR(ENOMEM);
        goto end;
    }
 
    if (dec_ctx->codec_type == AVMEDIA_TYPE_VIDEO) {
        buffersrc = avfilter_get_by_name("buffer");
        buffersink = avfilter_get_by_name("buffersink");
        
        dec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        logging("%d %d",dec_ctx->pix_fmt,dec_ctx->height);
        logging("%d %d",enc_ctx->pix_fmt,enc_ctx->height);
 
        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                dec_ctx->time_base.num, dec_ctx->time_base.den,
                dec_ctx->sample_aspect_ratio.num,
                dec_ctx->sample_aspect_ratio.den);
 
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
       
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
       
 
        ret = av_opt_set_bin(buffersink_ctx, "pix_fmts",
                (uint8_t*)&enc_ctx->pix_fmt, sizeof(enc_ctx->pix_fmt),
                AV_OPT_SEARCH_CHILDREN);
        
    } else if (dec_ctx->codec_type == AVMEDIA_TYPE_AUDIO) {
        buffersrc = avfilter_get_by_name("abuffer");
        buffersink = avfilter_get_by_name("abuffersink");
        
 
        if (!dec_ctx->channel_layout)
            dec_ctx->channel_layout =
                av_get_default_channel_layout(dec_ctx->channels);
        snprintf(args, sizeof(args),
                "time_base=%d/%d:sample_rate=%d:sample_fmt=%s:channel_layout=0x%"PRIx64,
                dec_ctx->time_base.num, dec_ctx->time_base.den, dec_ctx->sample_rate,
                av_get_sample_fmt_name(dec_ctx->sample_fmt),
                dec_ctx->channel_layout);
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                args, NULL, filter_graph);
        
 
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                NULL, NULL, filter_graph);
        
 
        ret = av_opt_set_bin(buffersink_ctx, "sample_fmts",
                (uint8_t*)&enc_ctx->sample_fmt, sizeof(enc_ctx->sample_fmt),
                AV_OPT_SEARCH_CHILDREN);
        
 
        ret = av_opt_set_bin(buffersink_ctx, "channel_layouts",
                (uint8_t*)&enc_ctx->channel_layout,
                sizeof(enc_ctx->channel_layout), AV_OPT_SEARCH_CHILDREN);
        
 
        ret = av_opt_set_bin(buffersink_ctx, "sample_rates",
                (uint8_t*)&enc_ctx->sample_rate, sizeof(enc_ctx->sample_rate),
                AV_OPT_SEARCH_CHILDREN);
        
    } else {
        ret = AVERROR_UNKNOWN;
        goto end;
    }
 
    outputs->name       = av_strdup("in");
    outputs->filter_ctx = buffersrc_ctx;
    outputs->pad_idx    = 0;
    outputs->next       = NULL;
 
    inputs->name       = av_strdup("out");
    inputs->filter_ctx = buffersink_ctx;
    inputs->pad_idx    = 0;
    inputs->next       = NULL;
 
    if ((ret = avfilter_graph_parse_ptr(filter_graph, filter_spec,
                    &inputs, &outputs, NULL)) < 0)
        goto end;
 
    if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
        goto end;
 
    
    fctx->buffersrc_context = buffersrc_ctx;
    fctx->buffersink_context = buffersink_ctx;
    fctx->filter_graph = filter_graph;
 
end:
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
 
    return ret;
}

void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame)
{
    FILE *pFile;
    char szFilename[32];
    int y;

     
    sprintf(szFilename, "thumbnail_frame_%d.png", iFrame);
    pFile = fopen(szFilename, "wb");
    if (pFile == NULL)
        return;

    // Write header
    fprintf(pFile, "P6\n%d %d\n255\n", width, height);

    // Write pixel data
    for (y = 0; y < height; y++)
        fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);

    // Close file
    fclose(pFile);
}

static int encode_write_frame(unsigned int stream_index, int flush)
{
    StreamContext *stream = &stream_context[stream_index];
    FilteringContext *filter = &filter_context[stream_index];
    AVFrame *filt_frame = flush ? NULL : filter->filtered_frame;
    AVPacket *enc_pkt = filter->encode_packet;
    int ret;
 
   
    av_packet_unref(enc_pkt);
 
    ret = avcodec_send_frame(stream->encode_context, filt_frame);
 
    if (ret < 0)
        return ret;
 
    while (ret >= 0) {
        ret = avcodec_receive_packet(stream->encode_context, enc_pkt);
 
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return 0;
 
        /* prepare packet for muxing */
        enc_pkt->stream_index = stream_index;
        av_packet_rescale_ts(enc_pkt,
                             stream->encode_context->time_base,
                             output_format_context->streams[stream_index]->time_base);
 
        
        /* mux encoded frame */
        ret = av_interleaved_write_frame(output_format_context, enc_pkt);
    }
 
    return ret;
}

static int filter_encode_write_frame(AVFrame *frame, unsigned int stream_index)
{
    FilteringContext *filter = &filter_context[stream_index];
    int ret;
 
    
    /* push the decoded frame into the filtergraph */
    ret = av_buffersrc_add_frame_flags(filter->buffersrc_context,
            frame, 0);
    
 
    /* pull filtered frames from the filtergraph */
    while (1) {
       
        ret = av_buffersink_get_frame(filter->buffersink_context,
                                      filter->filtered_frame);
        if (ret < 0) {
            /* if no more frames for output - returns AVERROR(EAGAIN)
             * if flushed and no more frames for output - returns AVERROR_EOF
             * rewrite retcode to 0 to show it as normal procedure completion
             */
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                ret = 0;
            break;
        }

        if(stream_index==0 && filter->filtered_frame->width)++thumbnail_frame;
        if(thumbnail_frame == chosen_frame)SaveFrame(filter->filtered_frame,filter->filtered_frame->height,filter->filtered_frame->width,thumbnail_frame);

        filter->filtered_frame->pict_type = AV_PICTURE_TYPE_NONE;
        ret = encode_write_frame(stream_index, 0);
        av_frame_unref(filter->filtered_frame);

        if (ret < 0)
            break;
        
    }
 
    return ret;
}

static int flush_encoder(unsigned int stream_index)
{
    if (!(stream_context[stream_index].encode_context->codec->capabilities &
                AV_CODEC_CAP_DELAY))
        return 0;
 
   
    return encode_write_frame(stream_index, 1);
}

int main(int argc, char **argv)
{

    int ret;
    AVPacket *packet = NULL;
    unsigned int stream_index;
    unsigned int i;

    // check for passed arguments
    {
        if (argc != 7)
        {
            logging("%d",argc);
            logging("Pass atleast 6 filename <input/output> to transcode");
            logging("inputfile outputfile thumbnailframe resolution_heigt resolution_width bitrate");
            logging("to skip any value put -1");
            return -1;
        }
    }
    char *p;

    chosen_frame = strtol(argv[3], &p, 10);
    res_h = strtol(argv[4], &p, 10);
    res_w = strtol(argv[5], &p, 10);
    bitrate = strtol(argv[6], &p, 10);
    
    errno = 0;
    if (errno != 0 || *p != '\0' || chosen_frame < 0){
        logging("Wrong parameter passed.3rd parameter must be an integer\n");
        return -1;
    }
    // open_input_file
    AVFormatContext *input_format_context;
    const char *input_file_name = argv[1];
    { // open_input_file
        int ret;
        // unsigned int i;

        input_format_context = NULL;

        if ((ret = avformat_open_input(&input_format_context, input_file_name, NULL, NULL)) < 0)
        {
            logging("Cannot opent input file\n");
            return ret;
        }

        stream_context = av_mallocz_array(input_format_context->nb_streams, sizeof(*stream_context));

        if (!stream_context)
        {
            logging("Couldnt allocate memory for stream context\n");
            return AVERROR(ENOMEM);
        }

        for (int i = 0; i < input_format_context->nb_streams; i++)
        {
            AVStream *stream = input_format_context->streams[i];
            // dump_stream_info(stream);
            AVCodec *decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            // dump_codec_info(decoder);

            if (!decoder)
            {
                logging("Failed to find decoder for stream_%d", i);
                return AVERROR_DECODER_NOT_FOUND;
            }

            AVCodecContext *codec_context;
            codec_context = avcodec_alloc_context3(decoder);

            if (!codec_context)
            {
                logging("failed to allocate the decoder context for stream_%d\n", i);
                return AVERROR(ENOMEM);
            }

            ret = avcodec_parameters_to_context(codec_context, stream->codecpar);
            if (ret < 0)
            {
                logging("Failed to copy decoder parameters to input decoder context for stream_%d\n", i);
                return ret;
            }

            if (codec_context->codec_type == AVMEDIA_TYPE_VIDEO || codec_context->codec_type == AVMEDIA_TYPE_AUDIO)
            {
                if (codec_context->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    codec_context->framerate = av_guess_frame_rate(input_format_context, stream, NULL);
                }

                ret = avcodec_open2(codec_context, decoder, NULL);

                if (ret < 0)
                {
                    logging("Failed to opend decoder for strem_%d", i);
                    return ret;
                }
            }

            stream_context[i].decode_context = codec_context;
            stream_context[i].decode_frame = av_frame_alloc();

            if (!stream_context[i].decode_frame)
            {
                logging("Error on allocating for stream context decode frame\n");
                return AVERROR(ENOMEM);
            }
        }

        // av_dump_format(input_format_context, 0, NULL, 0);
    }

    
    const char *output_filename = argv[2];
    { 

        AVStream *output_stream, *input_stream;
        AVCodecContext *decode_context, *encoder_context;
        AVCodec *encoder;

        int ret;

        output_format_context = NULL;
        avformat_alloc_output_context2(&output_format_context, NULL, NULL, output_filename);

        if (!output_format_context)
        {
            logging("Couldnt create output context\n");
            return AVERROR_UNKNOWN;
        }

       

        for (int i = 0; i < input_format_context->nb_streams; i++)
        {
            output_stream = avformat_new_stream(output_format_context, NULL);
            if (!output_stream)
            {
                logging("Failed allocation output stream\n");
                return AVERROR_UNKNOWN;
            }

            input_stream = input_format_context->streams[i];
            decode_context = stream_context[i].decode_context;

            if (decode_context->codec_type == AVMEDIA_TYPE_VIDEO || decode_context->codec_type == AVMEDIA_TYPE_AUDIO)
            {

                
                encoder = avcodec_find_encoder(decode_context->codec_id);
                
                encoder_context = avcodec_alloc_context3(encoder);
                
                logging("-----%d",decode_context->pix_fmt);
                if (decode_context->codec_type == AVMEDIA_TYPE_VIDEO)
                {
                    encoder_context->height = res_h > 0 ? res_h : decode_context->height;
                    encoder_context->width = res_w > 0 ? res_w : decode_context->width;
                    encoder_context->sample_aspect_ratio = decode_context->sample_aspect_ratio;
                    encoder_context->bit_rate = bitrate > 0 ? bitrate : decode_context->bit_rate;
                    encoder_context->time_base = av_inv_q(decode_context->framerate);
                    
                    if (encoder->pix_fmts)
                    {
                        encoder_context->pix_fmt = encoder->pix_fmts[0];
                    }
                    else
                    {
                        encoder_context->pix_fmt = decode_context->pix_fmt;
                    }
                }
                else
                {
                    encoder_context->sample_rate = decode_context->sample_rate;
                    encoder_context->channel_layout = decode_context->channel_layout;
                    encoder_context->channels = av_get_channel_layout_nb_channels(encoder_context->channel_layout);
                    encoder_context->sample_fmt = encoder->sample_fmts[0];
                    encoder_context->time_base = (AVRational){1, encoder_context->sample_rate};
                }

                if (output_format_context->oformat->flags & AVFMT_GLOBALHEADER)
                    encoder_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

                ret = avcodec_open2(encoder_context, encoder, NULL);
                ret = avcodec_parameters_from_context(output_stream->codecpar, encoder_context);
                output_stream->time_base = encoder_context->time_base;
                stream_context[i].encode_context = encoder_context;
            }
            else
            {
                ret = avcodec_parameters_copy(output_stream->codecpar, input_stream->codecpar);
                output_stream->time_base = input_stream->time_base;
            }
        }

        

       

        if (!(output_format_context->oformat->flags & AVFMT_NOFILE))
        {
            ret = avio_open(&output_format_context->pb, output_filename, AVIO_FLAG_WRITE);
           
        }
        ret = avformat_write_header(output_format_context, NULL);
        
    }
    
    
    
    
    { 
        const char *filter_spec;
        
        filter_context = av_malloc_array(input_format_context->nb_streams, sizeof(*filter_context));
        if (!filter_context)
            return AVERROR(ENOMEM);
         
        for (int i = 0; i < input_format_context->nb_streams; i++)
        {
            filter_context[i].buffersrc_context = NULL;
            filter_context[i].buffersink_context = NULL;
            filter_context[i].filter_graph = NULL;
            if (!(input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO || input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO))
                continue;
            
            if (input_format_context->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
                filter_spec = "null"; 
            else
                filter_spec = "anull"; 
              
            ret = init_filter(&filter_context[i], stream_context[i].decode_context,stream_context[i].encode_context, filter_spec);
             
            if (ret)
                return ret;

            filter_context[i].encode_packet = av_packet_alloc();
            if (!filter_context[i].encode_packet)
                return AVERROR(ENOMEM);

            filter_context[i].filtered_frame = av_frame_alloc();
            if (!filter_context[i].filtered_frame)
                return AVERROR(ENOMEM);
        }
    }
   
    if (!(packet = av_packet_alloc()))
        goto end;

    { 
        while (1)
        {
            if ((ret = av_read_frame(input_format_context, packet)) < 0)
                break;
            stream_index = packet->stream_index;
            

            if (filter_context[stream_index].filter_graph)
            {
                StreamContext *stream = &stream_context[stream_index];

                
                av_packet_rescale_ts(packet,
                                     input_format_context->streams[stream_index]->time_base,
                                     stream->decode_context->time_base);
                ret = avcodec_send_packet(stream->decode_context, packet);
                

                while (ret >= 0)
                {
                    ret = avcodec_receive_frame(stream->decode_context, stream->decode_frame);
                    if (ret == AVERROR_EOF || ret == AVERROR(EAGAIN))
                        break;
                    else if (ret < 0)
                        goto end;

                    stream->decode_frame->pts = stream->decode_frame->best_effort_timestamp;
                    ret = filter_encode_write_frame(stream->decode_frame, stream_index);
                    if (ret < 0)
                        goto end;
                }
            }
            else
            {
                
                av_packet_rescale_ts(packet,
                                     input_format_context->streams[stream_index]->time_base,
                                     output_format_context->streams[stream_index]->time_base);

                ret = av_interleaved_write_frame(output_format_context, packet);
                if (ret < 0)
                    goto end;
            }
            av_packet_unref(packet);
        }

        for (i = 0; i < input_format_context->nb_streams; i++) {
            
            if (!filter_context[i].filter_graph)
                continue;
            ret = filter_encode_write_frame(NULL, i);
            ret = flush_encoder(i);
            
        }
    
        av_write_trailer(output_format_context);
    }
end:
      av_packet_free(&packet);
    for (i = 0; i < input_format_context->nb_streams; i++) {
        avcodec_free_context(&stream_context[i].decode_context);
        if (output_format_context && output_format_context->nb_streams > i && output_format_context->streams[i] && stream_context[i].encode_context)
            avcodec_free_context(&stream_context[i].encode_context);
        if (filter_context && filter_context[i].filter_graph) {
            avfilter_graph_free(&filter_context[i].filter_graph);
            av_packet_free(&filter_context[i].encode_packet);
            av_frame_free(&filter_context[i].filtered_frame);
        }
 
        av_frame_free(&stream_context[i].decode_frame);
    }
    av_free(filter_context);
    av_free(stream_context);
    avformat_close_input(&input_format_context);
    if (output_format_context && !(output_format_context->oformat->flags & AVFMT_NOFILE))
        avio_closep(&output_format_context->pb);
    avformat_free_context(output_format_context);
 
    
 
    return ret ? 1 : 0;  
}