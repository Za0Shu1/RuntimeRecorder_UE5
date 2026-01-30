#include "LBRFFmpegEncodeThread.h"
#include "HAL/PlatformProcess.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY(LogFFmpegEncodeThread);

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

    Packet = av_packet_alloc();
    if (!Packet)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("Failed to alloc AVPacket"));
        return false;
    }

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

    // ================= Audio Init =================
    const AVCodec* AudioCodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!AudioCodec)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("AAC encoder not found"));
        return false;
    }

    AudioCodecCtx = avcodec_alloc_context3(AudioCodec);
    AudioCodecCtx->sample_rate = 48000;          // UE 默认
    AudioCodecCtx->sample_fmt = AudioCodec->sample_fmts[0]; // 通常 FLTP
    AudioCodecCtx->bit_rate = 128000;
    AudioCodecCtx->time_base = { 1, AudioCodecCtx->sample_rate };

    av_channel_layout_default(&AudioCodecCtx->ch_layout, 2);

    if (FormatCtx->oformat->flags & AVFMT_GLOBALHEADER)
    {
        AudioCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    if (avcodec_open2(AudioCodecCtx, AudioCodec, nullptr) < 0)
    {
        UE_LOG(LogFFmpegEncodeThread, Error, TEXT("Failed to open AAC codec"));
        return false;
    }

    AudioStream = avformat_new_stream(FormatCtx, nullptr);
    avcodec_parameters_from_context(AudioStream->codecpar, AudioCodecCtx);
    AudioStream->time_base = AudioCodecCtx->time_base;

    // ===== Swr =====
    SwrCtx = swr_alloc();

    AVChannelLayout InLayout;
    av_channel_layout_default(&InLayout, 2);

    swr_alloc_set_opts2(
        &SwrCtx,
        &AudioCodecCtx->ch_layout,
        AudioCodecCtx->sample_fmt,
        AudioCodecCtx->sample_rate,
        &InLayout,
        AV_SAMPLE_FMT_FLT, // 不是AV_SAMPLE_FMT_S16
        AudioCodecCtx->sample_rate,
        0,
        nullptr
    );

    swr_init(SwrCtx);

    // AAC 必须有固定 frame_size
    check(AudioCodecCtx->frame_size > 0);
    UE_LOG(LogFFmpegEncodeThread, Display,
        TEXT("AAC frame_size = %d"), AudioCodecCtx->frame_size);

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
            FrameEvent->Reset();  // 等待下次唤醒
        }

        FLBRRawFrame Frame;
        while (FrameQueue.Dequeue(Frame))
        {
            EncodeOneFrame(Frame);
        }

        FLBRAudioFrame AudioFrame;
        while (AudioQueue.Dequeue(AudioFrame))
        {
            EncodeOneAudioFrame(AudioFrame);
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

void FLBRFFmpegEncodeThread::PushAudioFrame(FLBRAudioFrame&& Frame)
{
    if (bStopAcceptFrame)
        return;

    AudioQueue.Enqueue(MoveTemp(Frame));

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

void FLBRFFmpegEncodeThread::EncodeOneAudioFrame(const FLBRAudioFrame& InAudio)
{
    if (!AudioCodecCtx || !AudioStream || !SwrCtx || !Packet)
    {
        return;
    }

    const int32 NumChannels = InAudio.NumChannels;
    if (NumChannels <= 0)
    {
        return;
    }

    UE_LOG(LogFFmpegEncodeThread, Warning,
        TEXT("[AudioDebug] samples=%d, first=%f"),
        InAudio.Samples.Num(),
        InAudio.Samples.Num() > 0 ? InAudio.Samples[0] : 0.f
    );

    // 1️ 先把 UE 给的 samples 全部攒起来
    PendingAudioSamples.Append(InAudio.Samples);

    const int32 AACFrameSamplesPerChannel = AudioCodecCtx->frame_size; // 1024
    const int32 AACFrameSamplesTotal = AACFrameSamplesPerChannel * NumChannels;

    // 2️ 只在「攒够 1024 * channel」时才送 AAC
    while (PendingAudioSamples.Num() >= AACFrameSamplesTotal)
    {
        // ---- 创建 AVFrame ----
        AVFrame* AVAudioFrame = av_frame_alloc();
        AVAudioFrame->nb_samples = AACFrameSamplesPerChannel;
        AVAudioFrame->format = AudioCodecCtx->sample_fmt;
        AVAudioFrame->sample_rate = AudioCodecCtx->sample_rate;
        AVAudioFrame->ch_layout = AudioCodecCtx->ch_layout;
        AVAudioFrame->pts = AudioFrameIndex;

        AudioFrameIndex += AACFrameSamplesPerChannel;

        if (av_frame_get_buffer(AVAudioFrame, 0) < 0)
        {
            UE_LOG(LogFFmpegEncodeThread, Error, TEXT("av_frame_get_buffer(audio) failed"));
            av_frame_free(&AVAudioFrame);
            return;
        }

        // ---- 输入数据（S16 interleaved）----
        const uint8* InData[1] =
        {
            reinterpret_cast<const uint8*>(PendingAudioSamples.GetData())
        };

        // ---- 重采样 / 格式转换 ----
        int Converted = swr_convert(
            SwrCtx,
            AVAudioFrame->data,
            AACFrameSamplesPerChannel,
            InData,
            AACFrameSamplesPerChannel
        );

        if (Converted <= 0)
        {
            UE_LOG(LogFFmpegEncodeThread, Error, TEXT("swr_convert failed"));
            av_frame_free(&AVAudioFrame);
            return;
        }

        // ---- 送给 AAC ----
        int Ret = avcodec_send_frame(AudioCodecCtx, AVAudioFrame);
        if (Ret < 0)
        {
            char Err[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(Ret, Err, sizeof(Err));
            UE_LOG(LogFFmpegEncodeThread, Error,
                TEXT("avcodec_send_frame(audio) failed: %S"), Err);

            av_frame_free(&AVAudioFrame);
            return;
        }

        // ---- 收包 ----
        while (avcodec_receive_packet(AudioCodecCtx, Packet) == 0)
        {
            av_packet_rescale_ts(
                Packet,
                AudioCodecCtx->time_base,
                AudioStream->time_base
            );

            Packet->stream_index = AudioStream->index;
            av_interleaved_write_frame(FormatCtx, Packet);
            av_packet_unref(Packet);
        }

        av_frame_free(&AVAudioFrame);

        // ---- 从缓存中移除已消费的 samples ----
        PendingAudioSamples.RemoveAt(
            0,
            AACFrameSamplesTotal,
            EAllowShrinking::No
        );
    }
}



void FLBRFFmpegEncodeThread::FlushEncoder()
{
    if (!Packet)
        return;

    // ================== Flush Video ==================
    if (CodecCtx && VideoStream)
    {
        avcodec_send_frame(CodecCtx, nullptr);

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

    // ================== Flush Audio ==================
    if (AudioCodecCtx && AudioStream)
    {
        const int32 NumChannels = AudioCodecCtx->ch_layout.nb_channels;
        const int32 FrameSamplesPerChannel = AudioCodecCtx->frame_size;
        const int32 FrameSamplesTotal = FrameSamplesPerChannel * NumChannels;

        // ① 先把 PendingAudioSamples 里剩余的 samples 补齐送进去
        if (PendingAudioSamples.Num() > 0)
        {
            if (PendingAudioSamples.Num() < FrameSamplesTotal)
            {
                PendingAudioSamples.AddZeroed(
                    FrameSamplesTotal - PendingAudioSamples.Num()
                );
            }

            FLBRAudioFrame LastFrame;
            LastFrame.NumChannels = NumChannels;
            LastFrame.Samples = MoveTemp(PendingAudioSamples);

            EncodeOneAudioFrame(LastFrame);
        }

        // ② 再真正 flush AAC encoder
        avcodec_send_frame(AudioCodecCtx, nullptr);

        while (avcodec_receive_packet(AudioCodecCtx, Packet) == 0)
        {
            av_packet_rescale_ts(
                Packet,
                AudioCodecCtx->time_base,
                AudioStream->time_base
            );

            Packet->stream_index = AudioStream->index;
            av_interleaved_write_frame(FormatCtx, Packet);
            av_packet_unref(Packet);
        }
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

    if (Packet)
    {
        av_packet_free(&Packet);
        Packet = nullptr;
    }

    if (FrameEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(FrameEvent);
        FrameEvent = nullptr;
    }

    if (SwrCtx)
    {
        swr_free(&SwrCtx);
    }

    if (AudioCodecCtx)
    {
        av_channel_layout_uninit(&AudioCodecCtx->ch_layout);
        avcodec_free_context(&AudioCodecCtx);
    }
}
