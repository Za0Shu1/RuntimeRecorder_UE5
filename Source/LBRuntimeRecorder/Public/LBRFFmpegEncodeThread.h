#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeBool.h"
#include "Containers/Queue.h"

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h> // av_opt_set
}

DECLARE_LOG_CATEGORY_EXTERN(LogFFmpegEncodeThread, Log, All);

struct FLBRRawFrame
{
    TArray<FColor> Pixels;   // UE ReadPixels µÃµ½µÄ FColor
    int32 Width = 0;
    int32 Height = 0;
    int64 PTS = 0;
};

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
    void StopRecording();

private:
    void EncodeOneFrame(const FLBRRawFrame& Frame);
    void FlushEncoder();
    void Cleanup();

private:
    int32 Width;
    int32 Height;
    int32 FPS;
    FString OutputFile;

    TQueue<FLBRRawFrame, EQueueMode::Mpsc> FrameQueue;
    FEvent* FrameEvent = nullptr;

    FThreadSafeBool bExit = false;
    FThreadSafeBool bStopAcceptFrame = false;

    int64 FrameIndex = 0;

    // FFmpeg
    AVFormatContext* FormatCtx = nullptr;
    AVCodecContext* CodecCtx = nullptr;
    AVStream* VideoStream = nullptr;
    SwsContext* SwsCtx = nullptr;
};
