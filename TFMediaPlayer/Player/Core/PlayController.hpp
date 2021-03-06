//
//  PlayController.hpp
//  TFMediaPlayer
//
//  Created by shiwei on 17/12/25.
//  Copyright © 2017年 shiwei. All rights reserved.
//

#ifndef PlayController_hpp
#define PlayController_hpp

extern "C"{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <stdio.h>
#include <string>
#include "Decoder.hpp"
#include "VTBDecoder.h"
#include <pthread.h>
#include <functional>
#include "DisplayController.hpp"
#include "TFMPAVFormat.h"
#include "AudioResampler.hpp"
#include "TFMPDebugFuncs.h"
#include "TFMPFrame.h"

namespace tfmpcore {
    
    bool videoFrameSizeNotified(RecycleBuffer<TFMPFrame *> *buffer, int curSize, bool isGreater,void *observer);
    typedef int (*FillAudioBufferFunc)(void *buffer, int64_t size, void *context);
    
    static int playResumeSize = 20;
    static int bufferEmptySize = 1;
    
    class PlayController{
        
        std::string mediaPath;
        
        AVFormatContext *fmtCtx;
        AVPacket packet;
        
        int videoStrem = -1;
        int audioStream = -1;
        int subTitleStream = -1;
        
#if EnableVTBDecode
        VTBDecoder *videoDecoder = nullptr;
#else
        Decoder *videoDecoder = nullptr;
#endif
        Decoder *audioDecoder = nullptr;
        Decoder *subtitleDecoder = nullptr;
        
        DisplayController *displayer = nullptr;
        
        TFMPMediaType desiredDisplayMediaType = TFMP_MEDIA_TYPE_ALL_AVIABLE;
        TFMPMediaType realDisplayMediaType = TFMP_MEDIA_TYPE_NONE;
        void calculateRealDisplayMediaType();

        double duration = 0;
        
        
        /*** A lot of controls and states ***/
        
        //0. prepare
        static int connectFail(void *opque);
        bool abortConnecting = false;
        bool prapareOK = false;
        //real audio format
        void resolveAudioStreamFormat();
        void setupSyncClock();
        
        //1. start
        void startReadingFrames();
        pthread_t readThread;
        static void * readFrame(void *context);
        
        //2. pause and resume
        bool paused = false;   //It's order from outerside, not state of player. In other word, it's a mark.
        bool readable = false;  //It's ability to read.
        pthread_cond_t read_cond = PTHREAD_COND_INITIALIZER;
        pthread_mutex_t read_mutex = PTHREAD_MUTEX_INITIALIZER;
        //Stop playing and start buffering when buffer is empty or seeking. This func'll be called when buffer is full again.
        void bufferDone();
        
        //3. stop
        bool stoping = false;
        
        //4. media resource is going to end. catch play ending
        bool checkingEnd = false;
        void startCheckPlayFinish();
        pthread_t signalThread;
        static void *signalPlayFinished(void *context);
        
        //5. seek
        pthread_t seekThread;
        static void * seekOperation(void *context);
        /**
         * The state of seeking.
         * It becomes true when the user drags the progressBar and loose fingers.
         * And it becomes false when displayer find the first frame whose pts is greater than the seeking time, because now is time we can actually resume playing.
         */
        bool seeking = false;
        bool prepareForSeeking = false;
        double markTime = 0;  //The media time that seek to or start to pause.
        
        //6. free
        pthread_t freeThread;
        static void * freeResources(void *context);
        void resetStatus();
        bool reading = false;
        pthread_cond_t waitLoopCond = PTHREAD_COND_INITIALIZER;
        pthread_mutex_t waitLoopMutex = PTHREAD_MUTEX_INITIALIZER;

    public:
        
        ~PlayController(){
            stop();
            delete displayer;
        }
        
        friend void frameEmpty(Decoder *decoder, void* context);
        
        friend bool tfmpcore::videoFrameSizeNotified(RecycleBuffer<TFMPFrame *> *buffer, int curSize, bool isGreater,void *observer);
        
        /** controls **/
        
        bool connectAndOpenMedia(std::string mediaPath);
        void cancelConnecting();
        
        /** callback which call when playing stoped. The second param is reason code of the reason to stop:
         *  -1 error occur
         *  0 reaching file end
         *  1 stop by calling func stop()
         */
        std::function<void(PlayController*, int)> playStoped;
        
        void play();
        void pause(bool flag);
        void stop();
        
        void seekTo(double time);
        void seekByForward(double interval);
        std::function<void(PlayController*)>seekingEndNotify;
        
        std::function<void(PlayController*, bool)> bufferingStateChanged;
        
        /** properties **/
        
        double getDuration();
        double getCurrentTime();
        
        //the real value is affect by realDisplayMediaType. For example, there is no audio stream, isAudioMajor couldn't be true.
        bool isAudioMajor = true;
        
        void setDesiredDisplayMediaType(TFMPMediaType desiredDisplayMediaType);
        TFMPMediaType getRealDisplayMediaType(){
            return realDisplayMediaType;
        }
        
        
        void *displayContext = nullptr;
        TFMPVideoFrameDisplayFunc displayVideoFrame = nullptr;

        TFMPFillAudioBufferStruct getFillAudioBufferStruct();
        
        DisplayController *getDisplayer();
        /** The source part inputs source audio stream desc, the platform-special part return a audio stream desc that will be fine for both parts. */
        std::function<TFMPAudioStreamDescription(TFMPAudioStreamDescription)> negotiateAdoptedPlayAudioDesc;
        FillAudioBufferFunc getFillAudioBufferFunc();
    };
}



#endif /* PlayController_hpp */
