
#include <libavformat/avformat.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

// print out the steps and errors
static void logging(const char *fmt, ...);

int main(int argc, const char *argv[])
{

    if (argc < 2)
    {
        printf("Need a media file.\n");
        return -1;
    }

    logging("Init containers and codecs and protocols");

    AVFormatContext *pFormatContext = avformat_alloc_context();

    if (!pFormatContext)
    {
        logging("ERROR : could not allocate memory for FormatContext");
        return -1;
    }

    logging("Opening file (%s) and loading format (container) header", argv[1]);

    if (avformat_open_input(&pFormatContext, argv[1], NULL, NULL) != 0)
    {
        logging("ERROR : could not open file");
        return -1;
    }

    logging("format %s, duration %lld us, bit_rate %lld", pFormatContext->iformat->name, pFormatContext->duration, pFormatContext->bit_rate);

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
