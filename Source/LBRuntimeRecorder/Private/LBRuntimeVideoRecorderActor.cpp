// LBRuntimeVideoRecorderActor.cpp

#include "LBRuntimeVideoRecorderActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "RenderGraphBuilder.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"
#include <ImageUtils.h>

DEFINE_LOG_CATEGORY(LogLBRuntimeVideoRecorder);

ALBRuntimeVideoRecorderActor::ALBRuntimeVideoRecorderActor()
{
	PrimaryActorTick.bCanEverTick = true;

	CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("VideoRecorder"));
	CaptureComponent->SetupAttachment(RootComponent);

	// 默认不开启实时捕捉
	CaptureComponent->bCaptureEveryFrame = true;
	CaptureComponent->bCaptureOnMovement = true;

	// 设置CaptureSource为HDR模式 (保留更多暗部细节)
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;

	// 优化后处理设置，保留暗部细节
	CaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
	CaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram; // 更稳定的直方图曝光
	CaptureComponent->PostProcessSettings.AutoExposureMinBrightness = 0.01f; // 更低的暗部兜底，避免死黑
	CaptureComponent->PostProcessSettings.AutoExposureMaxBrightness = 4.0f;   // 更高的亮部上限，保留高光
	CaptureComponent->PostProcessSettings.AutoExposureSpeedUp = 5.0f;        // 曝光提升速度（加快临时开启时的响应）
	CaptureComponent->PostProcessSettings.AutoExposureSpeedDown = 2.0f;      // 曝光降低速度
}

void ALBRuntimeVideoRecorderActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	// 更新当前分辨率
	FIntPoint Resolution = GetResolutionFromEnum(VideoResolution);
	CurrentWidth = Resolution.X;
	CurrentHeight = Resolution.Y;

	InitRenderTarget();
}

void ALBRuntimeVideoRecorderActor::BeginPlay()
{
	Super::BeginPlay();

	// 更新当前分辨率
	FIntPoint Resolution = GetResolutionFromEnum(VideoResolution);
	CurrentWidth = Resolution.X;
	CurrentHeight = Resolution.Y;

	InitRenderTarget();
}

#if WITH_EDITOR
void ALBRuntimeVideoRecorderActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FName PropertyName = (PropertyChangedEvent.Property != nullptr)
		? PropertyChangedEvent.Property->GetFName()
		: NAME_None;

	// 当分辨率属性变化时，更新RenderTarget
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ALBRuntimeVideoRecorderActor, VideoResolution))
	{
		FIntPoint Resolution = GetResolutionFromEnum(VideoResolution);
		CurrentWidth = Resolution.X;
		CurrentHeight = Resolution.Y;

		InitRenderTarget();
	}
}
#endif

void ALBRuntimeVideoRecorderActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsRecording) return;

	if (!RenderTarget) return;

	TimeAccumulator += DeltaTime;

	while (TimeAccumulator >= FrameInterval)
	{
		CaptureFrameAsync();

		TimeAccumulator -= FrameInterval;
	}
}

void ALBRuntimeVideoRecorderActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ALBRuntimeVideoRecorderActor::StartRecording(const FString& FileName)
{
	if (bIsRecording) return;

	bIsRecording = true;
	FrameInterval = 1.f / CaptureFPS;

	const FString SaveDir = GetVideoStoragePath();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*SaveDir))
	{
		PlatformFile.CreateDirectoryTree(*SaveDir);
	}

	FString FilePath = FPaths::Combine(SaveDir, FileName + TEXT(".mp4"));


	EncodeThread = new FLBRFFmpegEncodeThread(
		CurrentWidth,
		CurrentHeight,
		CaptureFPS,
		FilePath
	);

	EncodeRunnable = FRunnableThread::Create(
		EncodeThread,
		TEXT("LBR_FFmpegEncodeThread"),
		0,
		TPri_AboveNormal
	);

	bIsRecording = true;
	TimeAccumulator = 0.f;

	UE_LOG(LogLBRuntimeVideoRecorder, Log, TEXT("Start recording at resolution %dx%d,Gamma[%f],Exposure[%f]"), CurrentWidth, CurrentHeight, Gamma, Exposure);
}

void ALBRuntimeVideoRecorderActor::StopRecording()
{
	if (!bIsRecording) return;

	bIsRecording = false;

	if (!EncodeRunnable || !EncodeThread)
		return;

	// 通知线程停止（会 Flush）
	EncodeThread->StopRecording();

	// 等待 Run() 完成
	EncodeRunnable->WaitForCompletion();

	delete EncodeRunnable;
	EncodeRunnable = nullptr;

	delete EncodeThread;
	EncodeThread = nullptr;
}

void ALBRuntimeVideoRecorderActor::SceneShot(const FString& FileName)
{
	FLBRSimpleAsyncCapture::CaptureAsync(
		RenderTarget,
		Gamma,
		Exposure,
		[this, FileName](const TArray<FColor>& Pixels, int32 Width, int32 Height)
		{
			if (Pixels.Num() == 0 || Width <= 0 || Height <= 0)
				return;

			FString FilePath = FPaths::Combine(GetSceneShotStoragePath(), FileName + TEXT(".png"));

			Async(EAsyncExecution::ThreadPool, [Pixels, Width, Height, FilePath]()
				{
					TArray64<uint8> PNGData;
					FImageUtils::PNGCompressImageArray(Width, Height, Pixels, PNGData);

					IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
					FFileHelper::SaveArrayToFile(PNGData, *FilePath);
				});
		}
	);
}

FString ALBRuntimeVideoRecorderActor::GetDateString(FString Format)
{
	return FDateTime::Now().ToString(*Format);
}

FIntPoint ALBRuntimeVideoRecorderActor::GetResolutionFromEnum(ELBRVideoResolution Resolution) const
{
	switch (Resolution)
	{
	case ELBRVideoResolution::Resolution_360p:
		return FIntPoint(640, 360);
	case ELBRVideoResolution::Resolution_480p:
		return FIntPoint(854, 480);
	case ELBRVideoResolution::Resolution_720pHD:
		return FIntPoint(1280, 720);
	case ELBRVideoResolution::Resolution_1080pFullHD:
		return FIntPoint(1920, 1080);
	case ELBRVideoResolution::Resolution_1440p2K:
		return FIntPoint(2560, 1440);
	default:
		return FIntPoint(1920, 1080);
	}
}

void ALBRuntimeVideoRecorderActor::InitRenderTarget()
{
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(
			this,
			UTextureRenderTarget2D::StaticClass(),
			TEXT("VideoRenderTarget")
		);

		RenderTarget->RenderTargetFormat = RTF_RGBA8;
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->Filter = TF_Bilinear;
		RenderTarget->ClearColor = FLinearColor::Black;
		RenderTarget->bGPUSharedFlag = false;
	}

	// 检查是否需要调整尺寸
	const bool bNeedResize = RenderTarget->SizeX != CurrentWidth ||
		RenderTarget->SizeY != CurrentHeight;

	if (bNeedResize)
	{
		// 重新初始化RenderTarget
		RenderTarget->ReleaseResource();
		RenderTarget->InitCustomFormat(CurrentWidth, CurrentHeight, PF_B8G8R8A8, false);
		UE_LOG(LogLBRuntimeVideoRecorder, Log, TEXT("Init render target format %dx%d"), CurrentWidth, CurrentHeight);
		RenderTarget->UpdateResourceImmediate(true);
	}

	// 确保CaptureComponent使用正确的RenderTarget
	if (CaptureComponent && CaptureComponent->TextureTarget != RenderTarget)
	{
		CaptureComponent->TextureTarget = RenderTarget;
	}
}

void FLBRSimpleAsyncCapture::CaptureAsync(
	UTextureRenderTarget2D* RenderTarget,
	float InGamma,
	float InExposure,
	TFunction<void(const TArray<FColor>&, int32, int32)> Callback)
{
	if (!RenderTarget) return;

	ENQUEUE_RENDER_COMMAND(LBR_LDR_Capture)(
		[RenderTarget, InGamma, InExposure, Callback](FRHICommandListImmediate& RHICmdList)
		{
			FTextureRenderTargetResource* RTResource =
				RenderTarget->GetRenderTargetResource();

			if (!RTResource) return;


			// 不使用RDG，直接使用RHI Readback（更简单稳定）
			FRHITexture* SourceTexture = RTResource->GetTextureRHI();
			FIntPoint TextureSize(RTResource->GetSizeX(), RTResource->GetSizeY());


			// 创建Readback
			TSharedPtr<FRHIGPUTextureReadback, ESPMode::ThreadSafe> Readback =
				MakeShared<FRHIGPUTextureReadback, ESPMode::ThreadSafe>(TEXT("LDRReadback"));

			// 发起异步拷贝
			Readback->EnqueueCopy(RHICmdList, SourceTexture);

			// ===== 轮询 Readback =====
			auto Poll = [Readback, TextureSize, InGamma, InExposure, Callback](auto&& Self) -> void
				{
					if (!Readback->IsReady())
					{
						// 短暂延迟后继续轮询
						FPlatformProcess::Sleep(0.001f); // 1ms延迟
						AsyncTask(ENamedThreads::ActualRenderingThread,
							[Self]() { Self(Self); });
						return;
					}


					int32 Width = 0, Height = 0;
					void* Data = Readback->Lock(Width, &Height);

					if (!Data)
					{
						Readback->Unlock();
						UE_LOG(LogLBRuntimeVideoRecorder, Error, TEXT("Failed to lock readback data"));
						return;
					}

					// 验证尺寸
					if (Width != TextureSize.X || Height != TextureSize.Y)
					{
						UE_LOG(LogLBRuntimeVideoRecorder, Warning, TEXT("Size mismatch: Expected %dx%d, Got %dx%d"),
							TextureSize.X, TextureSize.Y, Width, Height);
					}

					TArray<FColor> Pixels;
					const int32 TotalPixels = Width * Height;
					Pixels.SetNumUninitialized(TotalPixels);

					FMemory::Memcpy(Pixels.GetData(), Data, TotalPixels * sizeof(FColor));
					Readback->Unlock();


					// 调试输出
					if (Pixels.Num() == 0)
					{
						UE_LOG(LogLBRuntimeVideoRecorder, Warning, TEXT("Readback empty Pixels."));
					}

					// 转到后台线程处理 Gamma/Exposure  (这里捕获Pixels是const,所以加mutable)
					Async(EAsyncExecution::Thread, [Pixels = MoveTemp(Pixels), Width, Height, InGamma, InExposure, Callback]() mutable
						{
							const float InvGamma = 1.0f / InGamma;
							const float Exposure = InExposure;

							for (FColor& Pixel : Pixels)
							{
								float R = Pixel.R / 255.0f;
								float G = Pixel.G / 255.0f;
								float B = Pixel.B / 255.0f;

								R = FMath::Clamp(R * Exposure, 0.0f, 1.0f);
								G = FMath::Clamp(G * Exposure, 0.0f, 1.0f);
								B = FMath::Clamp(B * Exposure, 0.0f, 1.0f);

								R = FMath::Pow(R, InvGamma);
								G = FMath::Pow(G, InvGamma);
								B = FMath::Pow(B, InvGamma);

								Pixel.R = FMath::RoundToInt(R * 255);
								Pixel.G = FMath::RoundToInt(G * 255);
								Pixel.B = FMath::RoundToInt(B * 255);
							}

							// 最后回调到游戏线程
							AsyncTask(ENamedThreads::GameThread, [Pixels = MoveTemp(Pixels), Width, Height, Callback]()
								{
									Callback(Pixels, Width, Height);
								});
						});
				};

			AsyncTask(ENamedThreads::ActualRenderingThread,
				[Poll]() { Poll(Poll); });
		});
}

void ALBRuntimeVideoRecorderActor::CaptureFrameAsync()
{
	FLBRSimpleAsyncCapture::CaptureAsync(
		RenderTarget,
		Gamma,
		Exposure,
		[this](const TArray<FColor>& Pixels, int32 Width, int32 Height)
		{
			FLBRRawFrame Frame;
			Frame.Width = Width;
			Frame.Height = Height;
			Frame.PTS = FrameCounter++;
			Frame.Pixels = Pixels; // TArray<FColor> 拷贝


			if (EncodeThread)
			{
				UE_LOG(LogLBRuntimeVideoRecorder, Log, TEXT("Encode frame index [%lld]"), FrameCounter);
				EncodeThread->PushFrame(MoveTemp(Frame));
			}
		}
	);
}
