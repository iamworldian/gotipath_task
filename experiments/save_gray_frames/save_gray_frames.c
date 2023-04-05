
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// Compile command : gcc save_gray_frames.c -lavformat -lavcodec -lavutil
// print out the steps and errors
static void logging(const char *fmt, ...);

static void save_gray_frame(unsigned char *buf, int wrap, int xsize, int ysize, char *filename)
{
    FILE *f;
    int i;
    f = fopen(filename,"w");
    // writing the minimal required header for a pgm file format
    // portable graymap format -> https://en.wikipedia.org/wiki/Netpbm_format#PGM_example
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);

    // writing line by line
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static int decode_packet(AVPacket *pPacket, AVCodecContext *pCodecContext, AVFrame *pFrame){
    int response = avcodec_send_packet(pCodecContext , pPacket);

    if(response < 0){
        logging("Error while sending a packet to the decoder: %s", av_err2str(response));
        return response;
    }

    while(response >=0){
        response = avcodec_receive_frame(pCodecContext , pFrame);
        if(response == AVERROR(EAGAIN) || response == AVERROR_EOF) {
            break;
        }else if(response < 0){
            logging("Error while receiving a frame from the decoder : %s", av_err2str(response));
            return response;
        }

        if (response >= 0) {
            logging(
                "Frame %d (type=%c, size=%d bytes, format=%d) pts %d key_frame %d [DTS %d]",
                pCodecContext->frame_number,
                av_get_picture_type_char(pFrame->pict_type),
                pFrame->pkt_size,
                pFrame->format,
                pFrame->pts,
                pFrame->key_frame,
                pFrame->coded_picture_number
            );

            char frame_filename[1024];
            snprintf(frame_filename, sizeof(frame_filename), "%s-%d.pgm", "frame", pCodecContext->frame_number);
            // Check if the frame is a planar YUV 4:2:0, 12bpp
            // That is the format of the provided .mp4 file
            // RGB formats will definitely not give a gray image
            // Other YUV image may do so, but untested, so give a warning
            if (pFrame->format != AV_PIX_FMT_YUV420P)
            {
                logging("Warning: the generated file may not be a grayscale image, but could e.g. be just the R component if the video format is RGB");
            }
            // save a grayscale frame into a .pgm file
            save_gray_frame(pFrame->data[0], pFrame->linesize[0], pFrame->width, pFrame->height, frame_filename);
            
        }
    }
    return 0;
}

int main(int argc, const char *argv[])
{

    if (argc < 2)
    {
        printf("Need a media file.\n");
        return -1;
    }

    const char *filename = argv[1];

    logging("Init containers and codecs and protocols");

    AVFormatContext *pFormatContext = avformat_alloc_context();

    if (!pFormatContext)
    {
        logging("ERROR : could not allocate memory for FormatContext");
        return -1;
    }

    logging("Opening file (%s) and loading format (container) header", filename);

    if (avformat_open_input(&pFormatContext, filename, NULL, NULL) != 0)
    {
        logging("ERROR : could not open file");
        return -1;
    }

    logging("format %s, duration %lld us, bit_rate %lld, nb_stream %u", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate,pFormatContext->nb_streams);

    logging("Finding stream info from format");

    if(avformat_find_stream_info(pFormatContext, NULL) < 0){
        logging("ERROR could not get the stream info");
        return -1;
    }

    // component that knows how to encode and decode the stream.It's the codec (audio or video)

    AVCodec *pCodec = NULL;

    //This struct describes the properties of an encoded stream.
    AVCodecParameters *pCodecParameters =  NULL;
    int video_stream_index = -1;


    // looping through all the streams and print its main information
    for(int i = 0 ; i < pFormatContext->nb_streams ; i++){
        AVCodecParameters *pLocalCodecParameters = pFormatContext->streams[i]->codecpar;

        logging("AVStream->time_base before open coded %d/%d", pFormatContext->streams[i]->time_base.num, pFormatContext->streams[i]->time_base.den);
        logging("AVStream->r_frame_rate before open coded %d/%d", pFormatContext->streams[i]->r_frame_rate.num, pFormatContext->streams[i]->r_frame_rate.den);
        logging("AVStream->start_time %" PRId64, pFormatContext->streams[i]->start_time);
        logging("AVStream->duration %" PRId64, pFormatContext->streams[i]->duration);

        logging("finding the proper decoder (CODEC)");

        // this component knows how to encode and decode the streams
        AVCodec *pLocalCodec = NULL;

        pLocalCodec = avcodec_find_decoder(pLocalCodecParameters->codec_id);

        if(pLocalCodec == NULL){
            logging("ERROR unsopported codec!");
            continue;
        }

        // when the stream is a video store its index,codec parameters and codec
        if(pLocalCodecParameters->codec_type == AVMEDIA_TYPE_VIDEO){
            if(video_stream_index == -1){
                video_stream_index = i;
                pCodec = pLocalCodec;
                pCodecParameters = pLocalCodecParameters;
            }

            logging("Video Codec: Resolution %d x %d" , pLocalCodecParameters->width , pLocalCodecParameters->height);
        }else if(pLocalCodecParameters->codec_type == AVMEDIA_TYPE_AUDIO){
            logging("Audio Codec: %d Channels, Sample Rate %d", pLocalCodecParameters->channels, pLocalCodecParameters->sample_rate);
        }

        // print its name,id and bitrate
        logging("\tCodec %s ID %d bit_rate %lld", pLocalCodec->name, pLocalCodec->id, pLocalCodecParameters->bit_rate);
    }

    if(video_stream_index == -1){
        logging("File %s doesnt contain a video stream!" , filename);
    }
    
    // With the codec, we can allocate memory for the AVCodecContext, which will hold the context for our decode/encode process, but then we need to fill this codec context with CODEC parameters; we do that with avcodec_parameters_to_context.

    AVCodecContext *pCodecContext = avcodec_alloc_context3(pCodec);

    if(!pCodecContext){
        logging("Failed to allocated memory for AVCodecContext");
        return -1;
    }

    if(avcodec_parameters_to_context(pCodecContext , pCodecParameters) < 0){
        logging("Failed to copy codec params to codec context");
        return -1;
    }

    if(avcodec_open2(pCodecContext,pCodec,NULL) < 0){
        logging("failed to open codec through avcodec_open2");
        return -1;
    }

    AVFrame *pFrame = av_frame_alloc();

    if(!pFrame){
        logging("failed to allocate memory for AVFrame");
        return -1;
    }

    AVPacket *pPacket = av_packet_alloc();

    if(!pPacket){
        logging("failed to allocate packet for AVPacket");
        return -1;
    }

    int response = 0;
    //int number_of_packets_to_process = 10;


    while(av_read_frame(pFormatContext , pPacket) >= 0){
        // if it is the video stream
        if(pPacket->stream_index == video_stream_index){
            //logging("AVPacket->pts %lld" , PRId64 , pPacket->pts);
            response = decode_packet(pPacket,pCodecContext,pFrame);

            if(response < 0)break;
            //if(--number_of_packets_to_process <= 0)break;
        }

        av_packet_unref(pPacket);
    }
    
    logging("releasing all the resources");

    avformat_close_input(&pFormatContext);
    av_packet_free(&pPacket);
    av_frame_free(&pFrame);
    avcodec_free_context(&pCodecContext);
    return 0;
}

static void logging(const char *fmt, ...)
{
    va_list args;
    fprintf(stderr, "LOG: ");
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}
