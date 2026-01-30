// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "LBRTypes.generated.h"

/**
 * 
 */

UENUM(BlueprintType)
enum class ELBRVideoResolution : uint8
{
	Resolution_360p      UMETA(DisplayName = "360p (640x360)"),
	Resolution_480p      UMETA(DisplayName = "480p (854x480)"),
	Resolution_720pHD    UMETA(DisplayName = "720p HD (1280x720)"),
	Resolution_1080pFullHD UMETA(DisplayName = "1080p Full HD (1920x1080)"),
	Resolution_1440p2K   UMETA(DisplayName = "1440p 2K (2560x1440)")
};

struct FLBRRawFrame
{
	TArray<FColor> Pixels;   // UE ReadPixels 得到的 FColor
	int32 Width = 0;
	int32 Height = 0;
	int64 PTS = 0;
};

struct FLBRAudioFrame
{
	TArray<float> Samples;   // interleaved
	int32 NumChannels;
	int32 SampleRate;
	int64 PTS;               // 样本级时间戳
};

UCLASS()
class LBRUNTIMERECORDER_API ULBRTypes : public UObject
{
	GENERATED_BODY()
};
