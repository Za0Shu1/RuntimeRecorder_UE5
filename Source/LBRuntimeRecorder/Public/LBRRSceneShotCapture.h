// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

class UTextureRenderTarget2D;

/**
 *
 */
class LBRUNTIMERECORDER_API LBRRSceneShotCapture
{
public:
	static void GetPixelsFromRenderTarget(UTextureRenderTarget2D* RenderTarget, TFunction<void(const TArray<FColor>&, int32, int32)> OnCaptureComplete, float Gamma = 2.2f, float Exposure = 1.5f);
	static void SavePixelsToPNG(const TArray<FColor>& Pixels, int32 Width, int32 Height, const FString& FilePath);
};
