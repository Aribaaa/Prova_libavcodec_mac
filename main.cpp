#include <iostream>
#include <SDL/SDL.h>
#include <SDL/SDL_thread.h>
#include <SDL/SDL_main.h>
#include <libswscale/swscale.h>
extern "C" {
#include <libavcodec/avcodec.h>
}
extern "C" {
#include <libavformat/avformat.h>
}
extern "C" {
#include <libavutil/avutil.h>
}
using namespace std;


int main (int argc, char *argv[]) {

    if(argc < 2){
        cout << "Please specify an input file\n";
        return -1;
    }
    av_register_all();

    /*
    OPENING THE FILE

    This registers all available file formats and codecs with the library so they will be used automatically when a file with the corresponding format/codec is opened.
    Note that you only need to call av_register_all() once, so we do it here in main(). If you like, it's possible to register only certain individual file formats and codecs,
    but there's usually no reason why you would have to do that.

    Now we can actually open the file:
     */

    AVFormatContext *pFormatCtx;
    // Open video file
    if(av_open_input_file(&pFormatCtx, argv[1], NULL, 0, NULL)!=0){
        cout << "Couldn't open the file\n";
        return -1;
    }

    /*
    We get our filename from the first argument. This function reads the file header and stores information about the file format in the AVFormatContext structure we have given it.
    The last three arguments are used to specify the file format, buffer size, and format options, but by setting this to NULL or 0, libavformat will auto-detect these.

    This function only looks at the header, so next we need to check out the stream information in the file.:
    */

    // Retrieve stream information
    if(av_find_stream_info(pFormatCtx)<0){
        cout <<"Couldn't find stream information\n";
        return -1;
    }

    /*
    This function populates pFormatCtx->streams with the proper information. We introduce a handy debugging function to show us what's inside:
    */

    // Dump information about file onto standard error
    dump_format(pFormatCtx, 0, argv[1], 0);

    /*
    Now pFormatCtx->streams is just an array of pointers, of size pFormatCtx->nb_streams, so let's walk through it until we find a video stream.
    */

    unsigned int i;
    AVCodecContext *pCodecCtx;

    // Find the first video stream
    int videoStream=-1;

    for(i=0; i<pFormatCtx->nb_streams; i++){
        if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_VIDEO) {
            videoStream=i;
            break;
        }
    }

    if(videoStream==-1){
        cout << "Didn't find a video stream\n";
        return -1;
    }
    else{
        // Get a pointer to the codec context for the video stream
        pCodecCtx=pFormatCtx->streams[videoStream]->codec;
    }

    /*
    The stream's information about the codec is in what we call the "codec context." This contains all the information about the codec that the stream is using,
    and now we have a pointer to it. But we still have to find the actual codec and open it:
    */

    AVCodec *pCodec;

    // Find the decoder for the video stream
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL) {
        cout << "Unsupported codec!\n";
        return -1; // Codec not found
    }
    // Open codec
    if(avcodec_open(pCodecCtx, pCodec)<0){
        cout << "Unsupported codec!\n";
        return -1; // Could not open codec
    }

    /*
    pCodecCtx->time_base now holds the frame rate information. time_base is a struct that has the numerator and denominator (AVRational).
    We represent the frame rate as a fraction because many codecs have non-integer frame rates (like NTSC's 29.97fps).

    STORING THE DATA
    Now we need a place to actually store the frame:
    */

    AVFrame *pFrame;

    // Allocate video frame
    pFrame=avcodec_alloc_frame();

    //-------------------------------------------------------------CUT HERE-----------------------------------------------------------------------------

    /*
    Since we're planning to output PPM files, which are stored in 24-bit RGB, we're going to have to convert our frame from its native format to RGB.
    ffmpeg will do these conversions for us. For most projects (including ours) we're going to want to convert our initial frame to a specific format.
    Let's allocate a frame for the converted frame now.
    */

    // Allocate an AVFrame structure
    /*AVFrame *pFrameRGB=avcodec_alloc_frame();
    if(pFrameRGB==NULL){
        return -1;
    }*/

    /*
    Even though we've allocated the frame, we still need a place to put the raw data when we convert it. We use avpicture_get_size to get the size we need,
    and allocate the space manually:
    */

    /*uint8_t *buffer;
    int numBytes;
    // Determine required buffer size and allocate buffer
    numBytes=avpicture_get_size(PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);
    buffer=(uint8_t *)av_malloc(numBytes*sizeof(uint8_t));*/

    /*
    av_malloc is ffmpeg's malloc that is just a simple wrapper around malloc that makes sure the memory addresses are aligned and such. It will not protect you from memory leaks,
    double freeing, or other malloc problems.

    Now we use avpicture_fill to associate the frame with our newly allocated buffer. About the AVPicture cast:
    the AVPicture struct is a subset of the AVFrame struct - the beginning of the AVFrame struct is identical to the AVPicture struct.
    */

    // Assign appropriate parts of buffer to image planes in pFrameRGB
    // Note that pFrameRGB is an AVFrame, but AVFrame is a superset
    // of AVPicture
    //avpicture_fill((AVPicture *)pFrameRGB, buffer, PIX_FMT_RGB24, pCodecCtx->width, pCodecCtx->height);

    /*
    Finally! Now we're ready to read from the stream!

    READING THE DATA
    What we're going to do is read through the entire video stream by reading in the packet, decoding it into our frame, and once our frame is complete, we will convert it.
    */

    /*int frameFinished;
    AVPacket packet;

    i=0;
    while(av_read_frame(pFormatCtx, &packet)>=0) {
      // Is this a packet from the video stream?
      if(packet.stream_index==videoStream) {
            // Decode video frame
        avcodec_decode_video(pCodecCtx, pFrame, &frameFinished,
                             packet.data, packet.size);

        // DA LEVARE

        // Did we get a video frame?
        if(frameFinished) {
        // Convert the image from its native format to RGB
            img_convert((AVPicture *)pFrameRGB, PIX_FMT_RGB24,
                (AVPicture*)pFrame, pCodecCtx->pix_fmt,
                            pCodecCtx->width, pCodecCtx->height);

            // Save the frame to disk
            if(++i<=5)
              SaveFrame(pFrameRGB, pCodecCtx->width,
                        pCodecCtx->height, i);
        }
      }

      // Free the packet that was allocated by av_read_frame
      av_free_packet(&packet);
    }*/


    //-----------------------------------------------------------------------------------CUT HERE---------------------------------------------------------------


    /*
    But first we have to start by seeing how to use the SDL Library. First we have to include the libraries and initalize SDL
    */

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        //fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        cout << "Could not initialize SDL - %s\n" + *SDL_GetError();
        exit(1);
    }

    /*
    SDL_Init() essentially tells the library what features we're going to use. SDL_GetError(), of course, is a handy debugging function

    CREATING A DISPLAY

    Now we need a place on the screen to put stuff. The basic area for displaying images with SDL is called a surface
    This sets up a screen with the given width and height. The next option is the bit depth of the screen - 0 is a special value that means "same as the current display".
    (This does not work on OS X; see source.)
    */

    SDL_Surface *screen;

#ifndef __DARWIN__
    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 0, 0);
#else
    screen = SDL_SetVideoMode(pCodecCtx->width, pCodecCtx->height, 24, 0);
#endif
    if(!screen) {
        cout << "SDL: could not set video mode - exiting\n";
        exit(1);
    }

    /*
    Now we create a YUV overlay on that screen so we can input video to it
    */

    SDL_Overlay     *bmp;

    bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height,
                               SDL_YV12_OVERLAY, screen);

    /*
    As we said before, we are using YV12 to display the image.

    DISPLAYING THE IMAGE

    Well that was simple enough! Now we just need to display the image. Let's go all the way down to where we had our finished frame.
    To display the image, we're going to make an AVPicture struct and set its data pointers and linesize to our YUV overlay
    What we're going to do is read through the entire video stream by reading in the packet, decoding it into our frame, and once our frame is complete, we will convert it.
    */

    int frameFinished;
    AVPacket packet;
    SDL_Rect rect;

    i=0;
    while(av_read_frame(pFormatCtx, &packet)>=0) {
        // Is this a packet from the video stream?
        if(packet.stream_index==videoStream) {
            // Decode video frame
            avcodec_decode_video(pCodecCtx, pFrame, &frameFinished, packet.data, packet.size);

            if(frameFinished) {
                SDL_LockYUVOverlay(bmp);

                AVPicture pict;
                pict.data[0] = bmp->pixels[0];
                pict.data[1] = bmp->pixels[2];
                pict.data[2] = bmp->pixels[1];

                pict.linesize[0] = bmp->pitches[0];
                pict.linesize[1] = bmp->pitches[2];
                pict.linesize[2] = bmp->pitches[1];

                // Convert the image into YUV format that SDL uses
                img_convert(&pict, PIX_FMT_YUV420P, (AVPicture *)pFrame, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

                SDL_UnlockYUVOverlay(bmp);

                rect.x = 0;
                rect.y = 0;
                rect.w = pCodecCtx->width;
                rect.h = pCodecCtx->height;
                SDL_DisplayYUVOverlay(bmp, &rect);

            }


        }

    }

    /*
    First, we lock the overlay because we are going to be writing to it. This is a good habit to get into so you don't have problems later.
    The AVPicture struct, as shown before, has a data pointer that is an array of 4 pointers. Since we are dealing with YUV420P here, we only have 3 channels,
    and therefore only 3 sets of data. Other formats might have a fourth pointer for an alpha channel or something. linesize is what it sounds like.
    The analogous structures in our YUV overlay are the pixels and pitches variables. ("pitches" is the term SDL uses to refer to the width of a given line of data.)
    So what we do is point the three arrays of pict.data at our overlay, so when we write to pict, we're actually writing into our overlay,
    which of course already has the necessary space allocated. Similarly, we get the linesize information directly from our overlay.
    We change the conversion format to PIX_FMT_YUV420P, and we use img_convert just like before.

    DRAWING THE IMAGE

    But we still need to tell SDL to actually show the data we've given it. We also pass this function a rectangle that says where the movie should go
    and what width and height it should be scaled to. This way, SDL does the scaling for us, and it can be assisted by your graphics processor for faster scaling
    Now our video is displayed!

    Let's take this time to show you another feature of SDL: its event system. SDL is set up so that when you type, or move the mouse in the SDL application,
    or send it a signal, it generates an event. Your program then checks for these events if it wants to handle user input. Your program can also make up events
    to send the SDL event system. This is especially useful when multithread programming with SDL, which we'll see in Tutorial 4. In our program,
    we're going to poll for events right after we finish processing a packet. For now, we're just going to handle the SDL_QUIT event so we can exit
    */

    SDL_Event event;

    av_free_packet(&packet);
    SDL_PollEvent(&event);
    switch(event.type) {
    case SDL_QUIT:
        SDL_Quit();
        exit(0);
        break;
    default:
        break;
    }

    // Close the codec
    avcodec_close(pCodecCtx);

    // Close the video file
    av_close_input_file(pFormatCtx);

    return 0;









}


