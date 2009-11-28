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
#define SDL_AUDIO_BUFFER_SIZE 1024;

typedef struct PacketQueue {
    AVPacketList *first_pkt, *last_pkt;
    int nb_packets;
    int size;
    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

int quit = 0; //global variable used to control the audio packet flow
PacketQueue audioq;

void packet_queue_init(PacketQueue *q) {
    memset(q, 0, sizeof(PacketQueue));
    q->mutex = SDL_CreateMutex();
    q->cond = SDL_CreateCond();
}

int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

    AVPacketList *pkt1;
    if(av_dup_packet(pkt) < 0) {
        return -1;
    }
    pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
    if (!pkt1){
        return -1;
    }
    pkt1->pkt = *pkt;
    pkt1->next = NULL;


    if(SDL_LockMutex(q->mutex)==0){

        if (!q->last_pkt){
            q->first_pkt = pkt1;
        }
        else{
            q->last_pkt->next = pkt1;
        }
        q->last_pkt = pkt1;
        q->nb_packets++;
        q->size += pkt1->pkt.size;
        SDL_CondSignal(q->cond);

        SDL_UnlockMutex(q->mutex);
        return 0;
    }
    else{
        return -1;
    }
}


static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
    AVPacketList *pkt1;
    int ret;

    if(SDL_LockMutex(q->mutex)==0){

        for(;;) {

            if(quit) {
                ret = -1;
                break;
            }

            pkt1 = q->first_pkt;
            if (pkt1) {
                q->first_pkt = pkt1->next;
                if (!q->first_pkt)
                    q->last_pkt = NULL;
                q->nb_packets--;
                q->size -= pkt1->pkt.size;
                *pkt = pkt1->pkt;
                av_free(pkt1);
                ret = 1;
                break;
            } else if (!block) {
                ret = 0;
                break;
            } else {
                SDL_CondWait(q->cond, q->mutex);
            }
        }
        SDL_UnlockMutex(q->mutex);
        return ret;
    }
    else{
        SDL_CondWait(q->cond, q->mutex);
        for(;;) {

            if(quit) {
                ret = -1;
                break;
            }

            pkt1 = q->first_pkt;
            if (pkt1) {
                q->first_pkt = pkt1->next;
                if (!q->first_pkt)
                    q->last_pkt = NULL;
                q->nb_packets--;
                q->size -= pkt1->pkt.size;
                *pkt = pkt1->pkt;
                av_free(pkt1);
                ret = 1;
                break;
            } else if (!block) {
                ret = 0;
                break;
            } else {
                SDL_CondWait(q->cond, q->mutex);
            }
        }
        SDL_UnlockMutex(q->mutex);
        return ret;
    }
}

int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf, int buf_size) {

    static AVPacket pkt;
    static uint8_t *audio_pkt_data = NULL;
    static int audio_pkt_size = 0;

    int len1, data_size;

    for(;;) {
        while(audio_pkt_size > 0) {
            data_size = buf_size;
            len1 = avcodec_decode_audio2(aCodecCtx, (int16_t *)audio_buf, &data_size,
                                         audio_pkt_data, audio_pkt_size);
            if(len1 < 0) {
                /* if error, skip frame */
                audio_pkt_size = 0;
                break;
            }
            audio_pkt_data += len1;
            audio_pkt_size -= len1;
            if(data_size <= 0) {
                /* No data yet, get more frames */
                continue;
            }
            /* We have data, return it and come back for more later */
            return data_size;
        }
        if(pkt.data)
            av_free_packet(&pkt);

        if(quit) {
            return -1;
        }

        if(packet_queue_get(&audioq, &pkt, 1) < 0) {
            return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
    }
}


void audio_callback(void *userdata, Uint8 *stream, int len) {

    AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
    int len1, audio_size;

    static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
    static unsigned int audio_buf_size = 0;
    static unsigned int audio_buf_index = 0;

    while(len > 0) {
        if(audio_buf_index >= audio_buf_size) {
            /* We have already sent all our data; get more */
            audio_size = audio_decode_frame(aCodecCtx, audio_buf,
                                            sizeof(audio_buf));
            if(audio_size < 0) {
                /* If error, output silence */
                audio_buf_size = 1024;
                memset(audio_buf, 0, audio_buf_size);
            } else {
                audio_buf_size = audio_size;
            }
            audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if(len1 > len)
            len1 = len;
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
    }
}



int main (int argc, char *argv[]) {

    SDL_Event event;

    if(argc < 2){
        cout << "Please specify an input file\n";
        return -1;
    }
    av_register_all();

    /*
    OPENING THE FILE

    This registers all available file formats and codecs with the library so they will be used
    automatically when a file with the corresponding format/codec is opened.
    Note that you only need to call av_register_all() once, so we do it here in main().
    If you like, it's possible to register only certain individual file formats and codecs,
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
    We get our filename from the first argument. This function reads the file header and stores
    information about the file format in the AVFormatContext structure we have given it.
    The last three arguments are used to specify the file format, buffer size, and format options,
    but by setting this to NULL or 0, libavformat will auto-detect these.

    This function only looks at the header, so next we need to check out the stream information
    in the file.:
    */

    // Retrieve stream information
    if(av_find_stream_info(pFormatCtx)<0){
        cout <<"Couldn't find stream information\n";
        return -1;
    }

    /*
    This function populates pFormatCtx->streams with the proper information.
    We introduce a handy debugging function to show us what's inside:
    */

    // Dump information about file onto standard error
    dump_format(pFormatCtx, 0, argv[1], 0);

    /*
    Now pFormatCtx->streams is just an array of pointers, of size pFormatCtx->nb_streams,
    so let's walk through it until we find a video/audio stream.
    */

    unsigned int i;
    AVCodecContext *pCodecCtx;
    AVCodecContext *aCodecCtx;

    // Find the first video stream
    int videoStream=-1;
    int audioStream=-1;


    for(i=0; i<pFormatCtx->nb_streams; i++){
        if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_VIDEO && videoStream < 0) {
            videoStream=i;
        }

        if(pFormatCtx->streams[i]->codec->codec_type==CODEC_TYPE_AUDIO && audioStream < 0) {
            audioStream=i;
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

    if(audioStream==-1){
        cout << "Didn't find an audio stream\n";
        return -1;
    }
    else{
        // Get a pointer to the codec context for the audio stream
        aCodecCtx=pFormatCtx->streams[audioStream]->codec;
    }

    /*
    The stream's information about the codec is in what we call the "codec context." This contains
    all the information about the codec that the stream is using,
    and now we have a pointer to it. But we still have to find the actual codec and open it:
    */

    AVCodec *pCodec;
    AVCodec *aCodec;


    // Find the decoder for the video stream
    pCodec=avcodec_find_decoder(pCodecCtx->codec_id);
    if(pCodec==NULL) {
        cerr << "Unsupported codec!\n";
        return -1; // Codec not found
    }
    // Open codec
    if(avcodec_open(pCodecCtx, pCodec)<0){
        cerr << "Unsupported codec!\n";
        return -1; // Could not open codec
    }

    //find the decoder for audio stream
    aCodec = avcodec_find_decoder(aCodecCtx->codec_id);
    if(!aCodec) {
        cerr << "Unsupported codec!\n";
        return -1;
    }
    //open codec
    if(avcodec_open(aCodecCtx, aCodec)){
        cerr << "Unsupported codec!\n";
        return -1; // Could not open codec
    }

    /*
    pCodecCtx->time_base now holds the frame rate information. time_base is a struct that has
    the numerator and denominator (AVRational).
    We represent the frame rate as a fraction because many codecs have non-integer frame rates
    (like NTSC's 29.97fps).

    STORING THE DATA
    Now we need a place to actually store the frame:
    */

    AVFrame *pFrame;

    // Allocate video frame
    pFrame=avcodec_alloc_frame();

    /*
    But first we have to start by seeing how to use the SDL Library. First we have to include the
    libraries and initalize SDL
    */

    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        //fprintf(stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        cout << "Could not initialize SDL - %s\n" + *SDL_GetError();
        exit(1);
    }

    /*
    SDL_Init() essentially tells the library what features we're going to use. SDL_GetError(), of
    course, is a handy debugging function

    Contained within *aCodecContext context is all the information we need to set up our audio
    */

    SDL_AudioSpec   wanted_spec, spec;

    wanted_spec.freq = aCodecCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCodecCtx->channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = SDL_AUDIO_BUFFER_SIZE;
    wanted_spec.callback = audio_callback;
    wanted_spec.userdata = aCodecCtx;

    if(SDL_OpenAudio(&wanted_spec, &spec) < 0) {
        fprintf(stderr, "SDL_OpenAudio: %s\n", SDL_GetError());
        cerr << "SDL_OpenAudio: \n" + *SDL_GetError();
        return -1;
    }

    packet_queue_init(&audioq);
    SDL_PauseAudio(0);

    /*
    CREATING A DISPLAY

    Now we need a place on the screen to put stuff. The basic area for displaying images with SDL
    is called a surface
    This sets up a screen with the given width and height. The next option is the bit depth of the
    screen - 0 is a special value that means "same as the current display".
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

    SDL_Overlay *bmp;

    bmp = SDL_CreateYUVOverlay(pCodecCtx->width, pCodecCtx->height, SDL_YV12_OVERLAY, screen);

    /*
    As we said before, we are using YV12 to display the image.

    DISPLAYING THE IMAGE

    Well that was simple enough! Now we just need to display the image. Let's go all the way down
    to where we had our finished frame.
    To display the image, we're going to make an AVPicture struct and set its data pointers and
    linesize to our YUV overlay
    What we're going to do is read through the entire video stream by reading in the packet,
    decoding it into our frame, and once our frame is complete, we will convert it.
    */

    int frameFinished;
    AVPacket packet;
    SDL_Rect rect;

    i=0;
    //loop used to fetch and decode audio and video frames
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
                //img_convert(&pict, PIX_FMT_YUV420P, (AVPicture *)pFrame, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height);

                SDL_UnlockYUVOverlay(bmp);

                rect.x = 0;
                rect.y = 0;
                rect.w = pCodecCtx->width;
                rect.h = pCodecCtx->height;
                SDL_DisplayYUVOverlay(bmp, &rect);

            }


        }
        //is this a packet from the audio stream?
        else if(packet.stream_index==audioStream) {
            packet_queue_put(&audioq, &packet);
        } else {
            av_free_packet(&packet);
        }


    }

    /*
    First, we lock the overlay because we are going to be writing to it. This is a good habit to get
    into so you don't have problems later.
    The AVPicture struct, as shown before, has a data pointer that is an array of 4 pointers. Since
    we are dealing with YUV420P here, we only have 3 channels,
    and therefore only 3 sets of data. Other formats might have a fourth pointer for an alpha channel
    or something. linesize is what it sounds like.
    The analogous structures in our YUV overlay are the pixels and pitches variables. ("pitches" is
    the term SDL uses to refer to the width of a given line of data.)
    So what we do is point the three arrays of pict.data at our overlay, so when we write to pict,
    we're actually writing into our overlay,
    which of course already has the necessary space allocated. Similarly, we get the linesize
    information directly from our overlay.
    We change the conversion format to PIX_FMT_YUV420P, and we use img_convert just like before.

    DRAWING THE IMAGE

    But we still need to tell SDL to actually show the data we've given it. We also pass this function
    a rectangle that says where the movie should go
    and what width and height it should be scaled to. This way, SDL does the scaling for us, and it
    can be assisted by your graphics processor for faster scaling
    Now our video is displayed!

    Let's take this time to show you another feature of SDL: its event system. SDL is set up so that
    when you type, or move the mouse in the SDL application,
    or send it a signal, it generates an event. Your program then checks for these events if it wants
    to handle user input. Your program can also make up events
    to send the SDL event system. This is especially useful when multithread programming with SDL,
    which we'll see in Tutorial 4. In our program,
    we're going to poll for events right after we finish processing a packet. For now, we're just
    going to handle the SDL_QUIT event so we can exit
    */



    /*
    AUDIO

    So now we want to play sound. SDL also gives us methods for outputting sound.
    The SDL_OpenAudio() function is used to open the audio device itself.
    It takes as arguments an SDL_AudioSpec struct, which contains all the information
    about the audio we are going to output.

    Before we show how you set this up, let's explain first about how audio is handled
    by computers. Digital audio consists of a long stream of samples. Each sample
    represents a value of the audio waveform. Sounds are recorded at a certain sample
    rate, which simply says how fast to play each sample, and is measured in number of
    samples per second. Example sample rates are 22,050 and 44,100 samples per second,
    which are the rates used for radio and CD respectively. In addition, most audio can
    have more than one channel for stereo or surround, so for example, if the sample is in stereo,
    the samples will come 2 at a time. When we get data from a movie file, we don't know how many
    samples we will get, but ffmpeg will not give us partial samples - that also means that it will
    not split a stereo sample up, either.

    SDL's method for playing audio is this: you set up your audio options: the sample rate
    (called "freq" for frequency in the SDL struct), number of channels, and so forth, and we
    also set a callback function and userdata. When we begin playing audio, SDL will continually
    call this callback function and ask it to fill the audio buffer with a certain number of bytes.
    After we put this information in the SDL_AudioSpec struct, we call SDL_OpenAudio(), which
    will open the audio device and give us back another AudioSpec struct. These are the specs we
    will actually be using — we are not guaranteed to get what we asked for!
    SETTING UP THE AUDIO

    Keep that all in your head for the moment, because we don't actually have any information
    yet about the audio streams yet! Let's go back to the place in our code where we found the
    video stream and find which stream is the audio stream.
    */

    /*
    QUEUES

    There! Now we're ready to start pulling audio information from the stream.
    But what do we do with that information? We are going to be continuously getting packets from
    the movie file, but at the same time SDL is going to call the callback function!
    The solution is going to be to create some kind of global structure that we can stuff audio
    packets in so our audio_callback has something to get audio data from! So what we're going to do
    is to create a queue of packets. ffmpeg even comes with a structure to help us with this:
    AVPacketList, which is just a linked list for packets. Here's our queue structure
    IMPLEMENTAZIONE PRIMA DEL MAIN
    */

    /*
    typedef struct PacketQueue {
      AVPacketList *first_pkt, *last_pkt;
      int nb_packets;
      int size;
      SDL_mutex *mutex;
      SDL_cond *cond;
    } PacketQueue;
    */

    /*
    First, we should point out that nb_packets is not the same as size — size refers to a byte size
    that we get from packet->size. You'll notice that we have a mutex and a condtion variable in there.
    This is because SDL is running the audio process as a separate thread. If we don't lock the queue
    properly, we could really mess up our data. We'll see how in the implementation of the queue.
    Every programmer should know how to make a queue, but we're including this so you can learn the
    SDL functions.

    First we make a function to initialize the queue
    IMPLEMENTAZIONE PRIMA DEL MAIN
    */

    /*
    void packet_queue_init(PacketQueue *q) {
      memset(q, 0, sizeof(PacketQueue));
      q->mutex = SDL_CreateMutex();
      q->cond = SDL_CreateCond();
    }
    */

    /*
    Then we will make a function to put stuff in our queue
    IMPLEMENTAZIONE PRIMA DEL MAIN
    */

    /*
    int packet_queue_put(PacketQueue *q, AVPacket *pkt) {

        AVPacketList *pkt1;
        if(av_dup_packet(pkt) < 0) {
            return -1;
        }
        pkt1 = (AVPacketList *)av_malloc(sizeof(AVPacketList));
        if (!pkt1){
            return -1;
        }
        pkt1->pkt = *pkt;
        pkt1->next = NULL;


        if(SDL_LockMutex(q->mutex)==0){

            if (!q->last_pkt){
                q->first_pkt = pkt1;
            }
            else{
                q->last_pkt->next = pkt1;
            }
            q->last_pkt = pkt1;
            q->nb_packets++;
            q->size += pkt1->pkt.size;
            SDL_CondSignal(q->cond);

            SDL_UnlockMutex(q->mutex);
            return 0;
        }
        else{
            return -1;
        }
    }
    */


    /*
    SDL_LockMutex() locks the mutex in the queue so we can add something to it, and then
    SDL_CondSignal() sends a signal to our get function (if it is waiting) through our condition
    variable to tell it that there is data and it can proceed, then unlocks the mutex to let it go.

    Here's the corresponding get function. Notice how SDL_CondWait() makes the function block
    (i.e. pause until we get data) if we tell it to
    IMPLEMENTAZIONE PRIMA DEL MAIN
    */

    /*
    static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block) {
        AVPacketList *pkt1;
        int ret;

        if(SDL_LockMutex(q->mutex)==0){

        for(;;) {

            if(quit) {
                ret = -1;
                break;
            }

            pkt1 = q->first_pkt;
            if (pkt1) {
                q->first_pkt = pkt1->next;
                if (!q->first_pkt)
                    q->last_pkt = NULL;
                q->nb_packets--;
                q->size -= pkt1->pkt.size;
                *pkt = pkt1->pkt;
                av_free(pkt1);
                ret = 1;
                break;
            } else if (!block) {
                ret = 0;
                break;
            } else {
                SDL_CondWait(q->cond, q->mutex);
            }
        }
        SDL_UnlockMutex(q->mutex);
        return ret;
    }
        else{
            SDL_CondWait(q->cond, q->mutex);
            for(;;) {

                if(quit) {
                    ret = -1;
                    break;
                }

                pkt1 = q->first_pkt;
                if (pkt1) {
                    q->first_pkt = pkt1->next;
                    if (!q->first_pkt)
                        q->last_pkt = NULL;
                    q->nb_packets--;
                    q->size -= pkt1->pkt.size;
                    *pkt = pkt1->pkt;
                    av_free(pkt1);
                    ret = 1;
                    break;
                } else if (!block) {
                    ret = 0;
                    break;
                } else {
                    SDL_CondWait(q->cond, q->mutex);
                }
            }
            SDL_UnlockMutex(q->mutex);
            return ret;
        }
    }

    */

    /*
    As you can see, we've wrapped the function in a forever loop so we will be sure to get some data
    if we want to block. We avoid looping forever by making use of SDL's SDL_CondWait() function.
    Basically, all CondWait does is wait for a signal from SDL_CondSignal() (or SDL_CondBroadcast())
    and then continue. However, it looks as though we've trapped it within our mutex — if we hold
    the lock, our put function can't put anything in the queue! However, what SDL_CondWait() also
    does for us is to unlock the mutex we give it and then attempt to lock it again once we get the
    signal.

    IN CASE OF FIRE

    You'll also notice that we have a global quit variable that we check to make sure that we
    haven't set the program a quit signal (SDL automatically handles TERM signals and the like).
    Otherwise, the thread will continue forever and we'll have to kill -9 the program.
    ffmpeg also has a function for providing a callback to check and see if we need to quit some
    blocking function: url_set_interrupt_cb

    FEEDING PACKETS

    The only thing left is to set up our queue

    packet_queue_init(&audioq);
    SDL_PauseAudio(0);
    DA INSERIRE PRIMA DI ENTRARE NEL LOOP CHE RECUPERA I FRAMES AUDIO-VIDEO E LI DECODIFICA
    */

    /*
    FETCHING PACKETS

    Now let's finally make our audio_callback function to fetch the packets on the queue.
    The callback has to be of the form void callback(void *userdata, Uint8 *stream, int len),
    where userdata of course is the pointer we gave to SDL, stream is the buffer we will be writing
    audio data to, and len is the size of that buffer. Here's the code
    */

    /*
    void audio_callback(void *userdata, Uint8 *stream, int len) {

      AVCodecContext *aCodecCtx = (AVCodecContext *)userdata;
      int len1, audio_size;

      static uint8_t audio_buf[(AVCODEC_MAX_AUDIO_FRAME_SIZE * 3) / 2];
      static unsigned int audio_buf_size = 0;
      static unsigned int audio_buf_index = 0;

      while(len > 0) {
        if(audio_buf_index >= audio_buf_size) {
          // We have already sent all our data; get more
          audio_size = audio_decode_frame(aCodecCtx, audio_buf,
                                          sizeof(audio_buf));
          if(audio_size < 0) {
            // If error, output silence
            audio_buf_size = 1024;
            memset(audio_buf, 0, audio_buf_size);
          } else {
            audio_buf_size = audio_size;
          }
          audio_buf_index = 0;
        }
        len1 = audio_buf_size - audio_buf_index;
        if(len1 > len)
          len1 = len;
        memcpy(stream, (uint8_t *)audio_buf + audio_buf_index, len1);
        len -= len1;
        stream += len1;
        audio_buf_index += len1;
      }
    }
    */

    /*
    This is basically a simple loop that will pull in data from another function we will write,
    audio_decode_frame(), store the result in an intermediary buffer, attempt to write len bytes to
    stream, and get more data if we don't have enough yet, or save it for later if we have some left
    over. The size of audio_buf is 1.5 times the size of the largest audio frame that ffmpeg will
    give us, which gives us a nice cushion.
    IMPLEMENTATA PRIMA DEL MAIN

    FINALLY DECODING THE AUDIO

    Let's get to the real meat of the decoder, audio_decode_frame
    */

    /*
    int audio_decode_frame(AVCodecContext *aCodecCtx, uint8_t *audio_buf,
                           int buf_size) {

      static AVPacket pkt;
      static uint8_t *audio_pkt_data = NULL;
      static int audio_pkt_size = 0;

      int len1, data_size;

      for(;;) {
        while(audio_pkt_size > 0) {
          data_size = buf_size;
          len1 = avcodec_decode_audio2(aCodecCtx, (int16_t *)audio_buf, &data_size,
                                      audio_pkt_data, audio_pkt_size);
          if(len1 < 0) {
            // if error, skip frame
            audio_pkt_size = 0;
            break;
          }
          audio_pkt_data += len1;
          audio_pkt_size -= len1;
          if(data_size <= 0) {
            // No data yet, get more frames
            continue;
          }
          // We have data, return it and come back for more later
          return data_size;
        }
        if(pkt.data)
          av_free_packet(&pkt);

        if(quit) {
          return -1;
        }

        if(packet_queue_get(&audioq, &pkt, 1) < 0) {
          return -1;
        }
        audio_pkt_data = pkt.data;
        audio_pkt_size = pkt.size;
      }
    }
    */

    /*
    This whole process actually starts towards the end of the function, where we call
    packet_queue_get(). We pick the packet up off the queue, and save its information. Then, once we
    have a packet to work with, we call avcodec_decode_audio2(), which acts a lot like its sister
    function, avcodec_decode_video(), except in this case, a packet might have more than one frame,
    so you may have to call it several times to get all the data out of the packet.

    * A note: Why avcodec_decode_audio2? Because there used to be an avcodec_decode_audio, but it's
    now deprecated. The new function reads from the data_size variable to figure out how big
    audio_buf is.

    Also, remember the cast to audio_buf, because SDL gives an 8 bit int buffer,
    and ffmpeg gives us data in a 16 bit int buffer. You should also notice the difference between
    len1 and data_size. len1 is how much of the packet we've used, and data_size is the amount of
    raw data returned.

    When we've got some data, we immediately return to see if we still need to get more data from
    the queue, or if we are done. If we still had more of the packet to process, we save it for later.
    If we finish up a packet, we finally get to free that packet.

    So that's it! We've got audio being carried from the main read loop to the queue,
    which is then read by the audio_callback function, which hands that data to SDL, which SDL beams
    to your sound card
    */















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

    //free the yuv frame
    av_free(pFrame);

    // Close the codec
    avcodec_close(pCodecCtx);

    // Close the video file
    av_close_input_file(pFormatCtx);

    return 0;









}


