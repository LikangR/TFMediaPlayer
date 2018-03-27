//
//  DisplayController.cpp
//  TFMediaPlayer
//
//  Created by shiwei on 17/12/29.
//  Copyright © 2017年 shiwei. All rights reserved.
//

#include "DisplayController.hpp"
#include "TFMPDebugFuncs.h"
extern "C"{
#include <libavutil/time.h>
}

#define TFMPBufferReadLog(fmt,...) printf(fmt,__VA_ARGS__);printf("\n");

using namespace tfmpcore;

static double minExeTime = 0.01; //seconds
static int TFMPDisplayPauseInterval = 10000;

void DisplayController::startDisplay(){
    
    if (shareVideoBuffer == nullptr && shareAudioBuffer == nullptr) {
        return;
    }
    
    if ((shareVideoBuffer != nullptr && displayVideoFrame == nullptr) ||
        (shareAudioBuffer != nullptr && displayVideoFrame == nullptr))
    {
        return;
    }
    
    shouldDisplay = true;
    
    TFMPDLOG_C("shouldDisplay to true\n");
    
    bool showVideo = displayMediaType & TFMP_MEDIA_TYPE_VIDEO;
    
    if (showVideo) {
        pthread_create(&dispalyThread, nullptr, displayLoop, this);
        pthread_detach(dispalyThread);
    }
}

void DisplayController::stopDisplay(){
    shouldDisplay = false;
    printf("shouldDisplay to false\n");
}

void DisplayController::flush(){
    
    remainingAudioBuffers.validSize = 0;
    remainingAudioBuffers.readIndex = 0;
}

void DisplayController::pause(bool flag){
    paused = flag;
    if (paused) {
        syncClock->reset();
    }
}

double DisplayController::getLastPlayTime(){
    if (videoTimeBase.den == 0 || videoTimeBase.num == 0) {
        return 0;
    }
    
    double lastPMediaTime = lastPts * av_q2d(lastIsAudio?audioTimeBase:videoTimeBase);
    return lastPMediaTime;
}

void DisplayController::freeResources(){
    
    if (shouldDisplay){
        TFMPDLOG_C("free DisplayController resource before stop display\n");
        shouldDisplay = false;
    }
    
    while (isDispalyingVideo || isFillingAudio) {
        av_usleep(10000); //0.01s
        TFMPDLOG_C("waiting %s displaying loop end\n",isDispalyingVideo?"video":"audio");
    }
    
    if(audioResampler) audioResampler->freeResources();
    
    free(remainingAudioBuffers.head);
    remainingAudioBuffers.validSize = 0;
    remainingAudioBuffers.readIndex = 0;
    
    displayContext = nullptr;
    shareVideoBuffer = nullptr;
    shareAudioBuffer = nullptr;
    displayMediaType = TFMP_MEDIA_TYPE_ALL_AVIABLE;
}

void *DisplayController::displayLoop(void *context){
    
    DisplayController *displayer = (DisplayController *)context;
    
    AVFrame *videoFrame = nullptr;
    
    while (displayer->shouldDisplay) {
        
        while (displayer->paused) {
//            TFMPDLOG_C("display pause video\n");
            av_usleep(TFMPDisplayPauseInterval);
        }
        
        displayer->isDispalyingVideo = true;
        videoFrame = nullptr; //reset it
        
        displayer->shareVideoBuffer->blockGetOut(&videoFrame);
        
        if (videoFrame == nullptr) continue;
        
        double remainTime = displayer->syncClock->remainTimeForVideo(videoFrame->pts, displayer->videoTimeBase);
        TFMPDLOG_C("remainTime: %.6f\n",remainTime);
        if (remainTime > minExeTime) {
            av_usleep(remainTime*1000000);
        }else if (remainTime < -minExeTime){
            av_frame_free(&videoFrame);
            TFMPDLOG_C("discard video frame\n");
            continue;
        }
        
        
        TFMPVideoFrameBuffer *interimBuffer = new TFMPVideoFrameBuffer();
        interimBuffer->width = videoFrame->width;
        //TODO: when should i cut one line of data to avoid the green data-empty zone in bottom?
        interimBuffer->height = videoFrame->height-1;
        
        for (int i = 0; i<AV_NUM_DATA_POINTERS; i++) {
            
            interimBuffer->pixels[i] = videoFrame->data[i]+videoFrame->linesize[i];
            interimBuffer->linesize[i] = videoFrame->linesize[i];
        }
        
        //TODO: unsupport format
        if (videoFrame->format == AV_PIX_FMT_YUV420P) {
            interimBuffer->format = TFMP_VIDEO_PIX_FMT_YUV420P;
        }else if (videoFrame->format == AV_PIX_FMT_NV12){
            interimBuffer->format = TFMP_VIDEO_PIX_FMT_NV12;
        }else if (videoFrame->format == AV_PIX_FMT_NV21){
            interimBuffer->format = TFMP_VIDEO_PIX_FMT_NV21;
        }else if (videoFrame->format == AV_PIX_FMT_RGB32){
            interimBuffer->format = TFMP_VIDEO_PIX_FMT_RGB32;
        }
        
        
        if (displayer->shouldDisplay){
            displayer->displayVideoFrame(interimBuffer, displayer->displayContext);
            
            if (!displayer->syncClock->isAudioMajor) {
                displayer->lastPRealTime = av_gettime_relative();
                displayer->lastPts = videoFrame->pts;
                displayer->lastIsAudio = false;
            }
            
            if(!displayer->paused) displayer->syncClock->presentVideo(videoFrame->pts, displayer->videoTimeBase);
        }
        
        av_frame_free(&videoFrame);
        
        displayer->isDispalyingVideo = false;
    }
    
    displayer->isDispalyingVideo = false;
    
    return 0;
}

static double fillAudioTime = 0;

int DisplayController::fillAudioBuffer(uint8_t **buffersList, int lineCount, int oneLineSize, void *context){
    
    double currentTime = av_gettime_relative()/1000000.0;
    TFMPDLOG_C("fill interval: %.6f\n",currentTime-fillAudioTime);
    fillAudioTime = currentTime;
    
    DisplayController *displayer = (DisplayController *)context;
    
    if (!displayer->shouldDisplay){
        return 0;
    }
    
    TFMPDLOG_C("\n\nfillAudioBuffer1[%dx%d]\n",oneLineSize,lineCount);
    
    uint8_t *buffer = buffersList[0];
    if (displayer->paused) {
        TFMPDLOG_C("display pause audio\n");
        memset(buffer, 0, oneLineSize);
        return 0;
    }
    
    displayer->isFillingAudio = true;
    
    TFMPRemainingBuffer *remainingBuffer = &(displayer->remainingAudioBuffers);
    uint32_t unreadSize = remainingBuffer->unreadSize();
    
    if (unreadSize >= oneLineSize) {
        memcpy(buffer, remainingBuffer->readingPoint(), oneLineSize);
        
        remainingBuffer->readIndex += oneLineSize;
        
    }else{
        int needReadSize = oneLineSize;
        if (unreadSize > 0) {
            needReadSize -= unreadSize;
            memcpy(buffer, remainingBuffer->readingPoint(), unreadSize);
            
            remainingBuffer->readIndex = 0;
            remainingBuffer->validSize = 0;
        }
        
        AVFrame *frame = nullptr;
        
        bool resample = false;
        uint8_t *dataBuffer = nullptr;
        int linesize = 0, outSamples = 0;
        
        while (needReadSize > 0) {
            //TODO: do more thing for planar audio.
            frame = nullptr;
            displayer->shareAudioBuffer->blockGetOut(&frame);
            
            //TODO: need more calm way to wait
            if (frame == nullptr) continue;
            
            TFMPDLOG_C(",%lld\n",frame->pts);
            
            double remainTime = displayer->syncClock->remainTimeForAudio(frame->pts, displayer->audioTimeBase);
            TFMPDLOG_C("remainTime audio: %.6f\n",remainTime);
            if (remainTime < -minExeTime){
                av_frame_free(&frame);
                TFMPDLOG_C("discard audio frame\n");
                continue;
            }
            
            if (displayer->audioResampler->isNeedResample(frame)) {
                if (displayer->audioResampler->reampleAudioFrame(frame, &outSamples, &linesize)) {
                    dataBuffer = displayer->audioResampler->resampledBuffers;
                    resample = true;
                    
                    TFMPBufferReadLog("resample %d, %d",linesize, outSamples);
                }
                
            }else{
                dataBuffer = frame->extended_data[0];
                linesize = frame->linesize[0];
            }
            
            if (dataBuffer == nullptr) {
                av_frame_free(&frame);
                continue;
            }
            
            TFMPDLOG_C("way 3\n");
            int filledSize = oneLineSize - needReadSize;
            int destSampleRate = displayer->audioResampler->adoptedAudioDesc.sampleRate;
            int destBytesPerChannel =  displayer->audioResampler->adoptedAudioDesc.bitsPerChannel/8;
            double preBufferDuration = (filledSize/destBytesPerChannel)/destSampleRate;
            
            //update sync clock
            if (displayer->syncClock->isAudioMajor) {
                displayer->lastPRealTime = av_gettime_relative();
                displayer->lastPts = frame->pts;
                displayer->lastIsAudio = true;
            }
            if(!displayer->paused) displayer->syncClock->presentAudio(frame->pts, displayer->audioTimeBase, preBufferDuration);
            
            
            if (needReadSize >= linesize) {
                
                //buffer has be copyed some data what size is oneLineSize - needReadSize.
                memcpy(buffer+(oneLineSize - needReadSize), dataBuffer, linesize);
                needReadSize -= linesize;
                
                av_frame_free(&frame);
                TFMPDLOG_C("way 5\n");
            }else{
                
                //there is a little buffer left.
                uint32_t remainingSize = linesize - needReadSize;
            
                //alloc a larger memory
                if (remainingBuffer->allocSize < remainingSize) {
                    free(remainingBuffer->head);
                    remainingBuffer->head = (uint8_t *)malloc(remainingSize);
                    remainingBuffer->allocSize = remainingSize;
                }
                
                remainingBuffer->readIndex = 0;
                remainingBuffer->validSize = remainingSize;
                
                memcpy(remainingBuffer->head, dataBuffer + needReadSize, remainingSize);
                
                
                memcpy(buffer+(oneLineSize - needReadSize), dataBuffer, needReadSize);
                needReadSize = 0;
                
                av_frame_free(&frame);
                TFMPDLOG_C("way 6\n");
            }
        }
    }
    
    TFMPDLOG_C("fillAudioBuffer2\n\n");
    
    displayer->isFillingAudio = false;
    
    return 0;
}

TFMPFillAudioBufferStruct DisplayController::getFillAudioBufferStruct(){
    return {fillAudioBuffer, this};
}




