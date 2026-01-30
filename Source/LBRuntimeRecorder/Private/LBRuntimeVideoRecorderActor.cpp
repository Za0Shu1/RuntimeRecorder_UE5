// LBRuntimeVideoRecorderActor.cpp

#include "LBRuntimeVideoRecorderActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Components/SceneCaptureComponent2D.h"
#include "LBRRSceneShotCapture.h"
#include "RenderGraphBuilder.h"
#include "RHIGPUReadback.h"
#include "RenderGraphUtils.h"

ALBRuntimeVideoRecorderActor::ALBRuntimeVideoRecorderActor()
{
	PrimaryActorTick.bCanEverTick = true;

	CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("VideoRecorder"));
	CaptureComponent->SetupAttachment(RootComponent);

	// 默认不开启实时捕捉
	CaptureComponent->bCaptureEveryFrame = true;
	CaptureComponent->bCaptureOnMovement = true;

	// 设置CaptureSource为HDR模式
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

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
	InitRenderTarget();
}

void ALBRuntimeVideoRecorderActor::BeginPlay()
{
	Super::BeginPlay();

}

void ALBRuntimeVideoRecorderActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bIsRecording) return;

	if (!RenderTarget) return;

	TimeAccumulator += DeltaTime;

	while (TimeAccumulator >= FrameInterval)
	{
		CaptureOneFrameAsync();

		TimeAccumulator -= FrameInterval;
	}
}

void ALBRuntimeVideoRecorderActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void ALBRuntimeVideoRecorderActor::CaptureOneFrame()
{
	const FString FilePath = FPaths::ProjectSavedDir() / TEXT("RuntimeRecorder") / TEXT("VideoCapture") /
		FString::Printf(TEXT("Frame_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%f")));

	LBRRSceneShotCapture::GetPixelsFromRenderTarget(
		RenderTarget,
		[this, FilePath](const TArray<FColor>& Pixels, int32 Width, int32 Height)
		{
			// 捕获完成回调，保存为PNG
			LBRRSceneShotCapture::SavePixelsToPNG(Pixels, Width, Height, FilePath);
		},
		Gamma,
		Exposure
	);
}

void ALBRuntimeVideoRecorderActor::CaptureOneFrameAsync()
{
	/*const FString FilePath = FPaths::ProjectSavedDir() / TEXT("RuntimeRecorder") / TEXT("VideoCapture") /
		FString::Printf(TEXT("Frame_%s.png"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S_%f")));*/

	const FString FilePath =
		FPaths::ProjectSavedDir() / TEXT("RuntimeRecorder") / TEXT("VideoCapture") /
		FString::Printf(TEXT("Frame_%lld.png"), FDateTime::Now().GetTicks());

	// 最简单的调用
	FLBRSimpleAsyncCapture::CaptureAsync(
		RenderTarget,
		Gamma,
		Exposure,
		[FilePath](const TArray<FColor>& Pixels, int32 Width, int32 Height)
		{
			// 保存PNG
			UE_LOG(LogTemp, Log, TEXT("SavePixelsToPNG"));
			LBRRSceneShotCapture::SavePixelsToPNG(Pixels, Width, Height, FilePath);
		}
	);
}

void ALBRuntimeVideoRecorderActor::StartRecording()
{
	if (bIsRecording) return;

	bIsRecording = true;
	FrameInterval = 1.f / CaptureFPS;

	const FString SaveDir = FPaths::ProjectSavedDir() / TEXT("RuntimeRecorder/VideoCapture");
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*SaveDir))
	{
		PlatformFile.CreateDirectoryTree(*SaveDir);
	}

	const FString FilePath = SaveDir / TEXT("Record.mp4");

	bIsRecording = true;
	TimeAccumulator = 0.f;
}

void ALBRuntimeVideoRecorderActor::StopRecording()
{
	if (!bIsRecording) return;

	bIsRecording = false;
}

void ALBRuntimeVideoRecorderActor::InitRenderTarget()
{
	if (!RenderTarget)
	{
		RenderTarget = NewObject<UTextureRenderTarget2D>(this, UTextureRenderTarget2D::StaticClass(), TEXT("VideoRenderTarget"));
		RenderTarget->RenderTargetFormat = RTF_RGBA8;
		RenderTarget->bAutoGenerateMips = false;
		RenderTarget->Filter = TF_Bilinear;
		RenderTarget->ClearColor = FLinearColor::Black;
		RenderTarget->InitAutoFormat(VideoWidth, VideoHeight);
		RenderTarget->UpdateResourceImmediate(true);
	}

	if (CaptureComponent)
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
						UE_LOG(LogTemp, Error, TEXT("Failed to lock readback data"));
						return;
					}

					// 验证尺寸
					if (Width != TextureSize.X || Height != TextureSize.Y)
					{
						UE_LOG(LogTemp, Warning, TEXT("Size mismatch: Expected %dx%d, Got %dx%d"),
							TextureSize.X, TextureSize.Y, Width, Height);
					}

					TArray<FColor> Pixels;
					const int32 TotalPixels = Width * Height;
					Pixels.SetNumUninitialized(TotalPixels);

					FMemory::Memcpy(Pixels.GetData(), Data, TotalPixels * sizeof(FColor));
					Readback->Unlock();


					// 调试输出
					if (Pixels.Num() > 0)
					{
						FColor FirstPixel = Pixels[0];
						UE_LOG(LogTemp, Log, TEXT("Async capture result: R=%d G=%d B=%d A=%d"),
							FirstPixel.R, FirstPixel.G, FirstPixel.B, FirstPixel.A);
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
									UE_LOG(LogTemp, Log, TEXT("Callback"));
									Callback(Pixels, Width, Height);
								});
						});
				};

			AsyncTask(ENamedThreads::ActualRenderingThread,
				[Poll]() { Poll(Poll); });
		});
}
