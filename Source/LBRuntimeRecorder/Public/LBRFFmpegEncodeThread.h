#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"
#include "LBRTypes.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h> // av_opt_set
#include <libswresample/swresample.h> //SwrContext
}

DECLARE_LOG_CATEGORY_EXTERN(LogFFmpegEncodeThread, Log, All);

class LBRUNTIMERECORDER_API FLBRFFmpegEncodeThread : public FRunnable
{
public:
    FLBRFFmpegEncodeThread(
        int32 InWidth,
        int32 InHeight,
        int32 InFPS,
        const FString& InOutputFile
    );

    virtual ~FLBRFFmpegEncodeThread();

    // FRunnable
    virtual bool Init() override;
    virtual uint32 Run() override;
    virtual void Stop() override;

    void PushFrame(FLBRRawFrame&& Frame);
    void PushAudioFrame(FLBRAudioFrame&& Frame);
    void StopRecording();

private:
    void EncodeOneFrame(const FLBRRawFrame& Frame);
    void EncodeOneAudioFrame(const FLBRAudioFrame& Frame);
    void FlushEncoder();
    void Cleanup();

private:
    AVPacket* Packet = nullptr;
    int32 Width;
    int32 Height;
    int32 FPS;
    FString OutputFile;

    TQueue<FLBRRawFrame, EQueueMode::Mpsc> FrameQueue;
    TQueue<FLBRAudioFrame, EQueueMode::Mpsc> AudioQueue;
    FEvent* FrameEvent = nullptr;

    FThreadSafeBool bExit = false;
    FThreadSafeBool bStopAcceptFrame = false;

    int64 FrameIndex = 0;

    // FFmpeg
    AVFormatContext* FormatCtx = nullptr;
    AVCodecContext* CodecCtx = nullptr;
    AVStream* VideoStream = nullptr;
    SwsContext* SwsCtx = nullptr;

    // ===== Audio =====
    AVCodecContext* AudioCodecCtx = nullptr;
    AVStream* AudioStream = nullptr;
    SwrContext* SwrCtx = nullptr;

    // AAC 需要的音频缓存
    TArray<float> PendingAudioSamples;
    // 音频 pts（单位：sample）
    int64 AudioFrameIndex = 0;
};
