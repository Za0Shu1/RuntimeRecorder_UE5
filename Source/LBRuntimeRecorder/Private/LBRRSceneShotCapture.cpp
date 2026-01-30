// Fill out your copyright notice in the Description page of Project Settings.


#include "LBRRSceneShotCapture.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHICommandList.h"
#include "ImageUtils.h"


void LBRRSceneShotCapture::GetPixelsFromRenderTarget(UTextureRenderTarget2D* RenderTarget, TFunction<void(const TArray<FColor>&, int32, int32)> OnCaptureComplete, float InGamma, float InExposure)
{
	if (!RenderTarget) return;

	FTextureRenderTargetResource* Resource =
		RenderTarget->GameThread_GetRenderTargetResource();

	const int32 Width = RenderTarget->SizeX;
	const int32 Height = RenderTarget->SizeY;

	// 提交渲染线程命令，读取像素（渲染线程操作必须在ENQUEUE_RENDER_COMMAND中）
	ENQUEUE_RENDER_COMMAND(LBRR_CaptureRT)(
		[Resource, Width, Height, OnCaptureComplete, InGamma, InExposure](FRHICommandListImmediate& RHICmdList)
		{
			TArray<FColor> Pixels;
			Pixels.SetNumUninitialized(Width * Height);

			FReadSurfaceDataFlags Flags(RCM_UNorm);
			Flags.SetLinearToGamma(false);

			RHICmdList.ReadSurfaceData(
				Resource->GetRenderTargetTexture(),
				FIntRect(0, 0, Width, Height),
				Pixels,
				Flags
			);

			//  Gamma校正：线性空间(Linear) → sRGB空间（解决PNG偏暗核心步骤）
			// 人眼对亮度的感知是非线性的，显示器默认使用sRGB（Gamma≈2.2），而RenderTarget是线性空间
			const float InvGamma = 1.0f / InGamma;
			const float Exposure = InExposure; // 提升曝光，让暗部变亮
			for (FColor& Pixel : Pixels)
			{
				// 先把0-255转为0-1的线性空间值
				float R = Pixel.R / 255.0f;
				float G = Pixel.G / 255.0f;
				float B = Pixel.B / 255.0f;

				// 1. 应用曝光补偿（提升暗部）
				R = FMath::Clamp(R * Exposure, 0.0f, 1.0f);
				G = FMath::Clamp(G * Exposure, 0.0f, 1.0f);
				B = FMath::Clamp(B * Exposure, 0.0f, 1.0f);

				// 2. Gamma校正（线性→sRGB）
				R = FMath::Pow(R, InvGamma);
				G = FMath::Pow(G, InvGamma);
				B = FMath::Pow(B, InvGamma);

				// 转回0-255
				Pixel.R = FMath::RoundToInt(R * 255);
				Pixel.G = FMath::RoundToInt(G * 255);
				Pixel.B = FMath::RoundToInt(B * 255);
			}

			// 回到游戏线程通知完成
			AsyncTask(ENamedThreads::GameThread, [Pixels, Width, Height, OnCaptureComplete]()
				{
					OnCaptureComplete(Pixels, Width, Height);
				});
		});
}

void LBRRSceneShotCapture::SavePixelsToPNG(const TArray<FColor>& Pixels, int32 Width, int32 Height, const FString& FilePath)
{
	if (Pixels.Num() == 0 || Width <= 0 || Height <= 0)
		return;

	Async(EAsyncExecution::ThreadPool, [Pixels, Width, Height, FilePath]()
		{
			TArray64<uint8> PNGData;
			FImageUtils::PNGCompressImageArray(Width, Height, Pixels, PNGData);

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);
			FFileHelper::SaveArrayToFile(PNGData, *FilePath);
		});
}
