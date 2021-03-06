//
//  TFAudioUnitPlayer.h
//  TFMediaPlayer
//
//  Created by shiwei on 18/1/8.
//  Copyright © 2018年 shiwei. All rights reserved.
//

/** A pull-mode audio player based on audio unit. */

#import <Foundation/Foundation.h>
#import "TFMPAVFormat.h"

typedef void(*TFMPShareAudioBufferFunc) (uint8_t **buffer, int size, void *context);
typedef struct{
    TFMPShareAudioBufferFunc shareAudioFunc;
    void *context;
} TFMPShareAudioBufferStruct;

@interface TFAudioUnitPlayer : NSObject

@property (nonatomic, assign) TFMPFillAudioBufferStruct fillStruct;

-(TFMPAudioStreamDescription)resultAudioDescForSource:(TFMPAudioStreamDescription)sourceDesc;

-(BOOL)play;

-(void)pause;

-(void)stop;

@property (nonatomic, assign) TFMPShareAudioBufferStruct shareAudioStruct;

@end
