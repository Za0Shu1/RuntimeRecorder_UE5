#include "LBRFFmpegEncodeThread.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY(LogFFmpegEncodeThread);

// 构造函数：提前初始化 FrameEvent
FLBRFFmpegEncodeThread::FLBRFFmpegEncodeThread(
    int32 InWidth,
    int32 InHeight,
    int32 InFPS,
    const FString& InOutputFile
)
    : Width(InWidth)
    , Height(InHeight)
    , FPS(InFPS)
    , OutputFile(InOutputFile)
    , bExit(false)
    , bStopAcceptFrame(false)
    , FrameIndex(0)
{
    FrameEvent = FPlatformProcess::GetSynchEventFromPool(false);
}

FLBRFFmpegEncodeThread::~FLBRFFmpegEncodeThread()
{
    Cleanup();
}

bool FLBRFFmpegEncodeThread::Init()
{
    avformat_alloc_output_context2(
        &FormatCtx,
        nullptr,
        "mp4",
        TCHAR_TO_UTF8(*OutputFile)
    );

    if (!FormatCtx)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("Failed to create format context"));
        return false;
    }

    const AVCodec* Codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!Codec)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("H264 encoder not found"));
        return false;
    }

    CodecCtx = avcodec_alloc_context3(Codec);
    CodecCtx->width = Width;
    CodecCtx->height = Height;
    CodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
    CodecCtx->time_base = { 1, FPS };
    CodecCtx->framerate = { FPS, 1 };
    CodecCtx->gop_size = FPS;
    CodecCtx->max_b_frames = 0;

    // 可选：降低延迟
    av_opt_set(CodecCtx->priv_data, "preset", "ultrafast", 0);

    if (avcodec_open2(CodecCtx, Codec, nullptr) < 0)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("Failed to open codec"));
        return false;
    }

    VideoStream = avformat_new_stream(FormatCtx, nullptr);
    avcodec_parameters_from_context(VideoStream->codecpar, CodecCtx);
    VideoStream->time_base = CodecCtx->time_base;

    if (!(FormatCtx->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&FormatCtx->pb, TCHAR_TO_UTF8(*OutputFile), AVIO_FLAG_WRITE) < 0)
        {
            UE_LOG(LogFFmpegEncodeThread, Error, TEXT("Failed to open output file"));
            return false;
        }
    }

    int Ret = avformat_write_header(FormatCtx, nullptr);
    if (Ret < 0)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("avformat_write_header failed: %d"), Ret);
        return false;
    }

    SwsCtx = sws_getContext(
        Width,
        Height,
        AV_PIX_FMT_BGRA,        // FColor = BGRA
        Width,
        Height,
        AV_PIX_FMT_YUV420P,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr
    );

    return true;
}

uint32 FLBRFFmpegEncodeThread::Run()
{
    while (!bExit || !FrameQueue.IsEmpty())
    {
        if (FrameEvent)
        {
            FrameEvent->Wait();
        }

        FLBRRawFrame Frame;
        while (FrameQueue.Dequeue(Frame))
        {
            EncodeOneFrame(Frame);
        }
    }

    FlushEncoder();
    if (FormatCtx)
    {
        av_write_trailer(FormatCtx);
    }
    Cleanup();

    return 0;
}

void FLBRFFmpegEncodeThread::Stop()
{
    bExit = true;
    if (FrameEvent)
    {
        FrameEvent->Trigger();
    }
}

void FLBRFFmpegEncodeThread::StopRecording()
{
    bStopAcceptFrame = true;
    bExit = true;
    if (FrameEvent)
    {
        FrameEvent->Trigger();
    }
}

void FLBRFFmpegEncodeThread::PushFrame(FLBRRawFrame&& Frame)
{
    if (bStopAcceptFrame)
        return;

    FrameQueue.Enqueue(MoveTemp(Frame));
    if (FrameEvent)
    {
        FrameEvent->Trigger();
    }
}

void FLBRFFmpegEncodeThread::EncodeOneFrame(const FLBRRawFrame& Raw)
{
    if (!CodecCtx)
        return;

    AVFrame* Frame = av_frame_alloc();
    Frame->format = CodecCtx->pix_fmt;
    Frame->width = CodecCtx->width;
    Frame->height = CodecCtx->height;
    Frame->pts = FrameIndex++;

    int Ret = av_frame_get_buffer(Frame, 32);
    if (Ret < 0)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("av_frame_get_buffer failed"));
        av_frame_free(&Frame);
        return;
    }

    uint8* SrcData[] =
    {
        reinterpret_cast<uint8*>(const_cast<FColor*>(Raw.Pixels.GetData()))
    };

    int SrcStride[] =
    {
        Raw.Width * 4
    };

    sws_scale(
        SwsCtx,
        SrcData,
        SrcStride,
        0,
        Raw.Height,
        Frame->data,
        Frame->linesize
    );

    avcodec_send_frame(CodecCtx, Frame);

    AVPacket* Packet = av_packet_alloc();
    if (!Packet) return;

    while (avcodec_receive_packet(CodecCtx, Packet) == 0)
    {
        av_packet_rescale_ts(
            Packet,
            CodecCtx->time_base,
            VideoStream->time_base
        );

        Packet->stream_index = VideoStream->index;
        av_interleaved_write_frame(FormatCtx, Packet);
        av_packet_unref(Packet);
    }

    av_frame_free(&Frame);
}

void FLBRFFmpegEncodeThread::FlushEncoder()
{
    if (!CodecCtx)
        return;

    avcodec_send_frame(CodecCtx, nullptr);

    AVPacket* Packet = av_packet_alloc();
    if (!Packet) return;

    while (avcodec_receive_packet(CodecCtx, Packet) == 0)
    {
        av_packet_rescale_ts(
            Packet,
            CodecCtx->time_base,
            VideoStream->time_base
        );

        Packet->stream_index = VideoStream->index;
        av_interleaved_write_frame(FormatCtx, Packet);
        av_packet_unref(Packet);
    }
}

void FLBRFFmpegEncodeThread::Cleanup()
{
    if (SwsCtx)
    {
        sws_freeContext(SwsCtx);
        SwsCtx = nullptr;
    }

    if (CodecCtx)
    {
        avcodec_free_context(&CodecCtx);
        CodecCtx = nullptr;
    }

    if (FormatCtx)
    {
        if (!(FormatCtx->oformat->flags & AVFMT_NOFILE))
        {
            avio_closep(&FormatCtx->pb);
        }

        avformat_free_context(FormatCtx);
        FormatCtx = nullptr;
    }

    if (FrameEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(FrameEvent);
        FrameEvent = nullptr;
    }
}
