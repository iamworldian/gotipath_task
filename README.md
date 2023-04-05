# gotipath_task

# Compiled the source with
```bash
gcc task_source.c -o out -lavcodec -lavformat -lavutil -lavfilter
```

# To Run the Code

```
./out.o inputFile.{extension} outputfile.{extension} frame_number_for_thumbnail output_res_height output_res_width output_bitrate
```

### if want to skip any arguments to pass or have default pass -1.
extension for hls is `.m3u8`.

## Sample command
```
./out.o video.mp4 out.m3u8 100 480 640 200000
```