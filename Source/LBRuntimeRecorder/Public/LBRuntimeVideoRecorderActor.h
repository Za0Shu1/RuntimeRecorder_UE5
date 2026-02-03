// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LBRFFmpegEncodeThread.h"
#include "LBRTypes.h"
#include "LBSubmixCapture.h"
#include "LBRuntimeVideoRecorderActor.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogLBRuntimeVideoRecorder, Log, All);

UCLASS()
class LBRUNTIMERECORDER_API ALBRuntimeVideoRecorderActor : public AActor
{
	GENERATED_BODY()

public:
	ALBRuntimeVideoRecorderActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Video Recorder")
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Video Recorder")
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Recorder", meta = (DisplayName = "分辨率"))
	ELBRVideoResolution VideoResolution = ELBRVideoResolution::Resolution_1080pFullHD;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Recorder", meta = (DisplayName = "Gamma", ClampMin = "0.1", ClampMax = "5.0"))
	float Gamma = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Recorder", meta = (DisplayName = "曝光补偿", ClampMin = "0.1", ClampMax = "5.0"))
	float Exposure = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Video Recorder", meta = (DisplayName = "帧率", ClampMin = "1", ClampMax = "120"))
	float CaptureFPS = 60.f;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "LBRuntimeVideoRecorder | Video Recorder")
	void StartRecording(const FString& FileName = "Output");

	UFUNCTION(BlueprintCallable, Category = "LBRuntimeVideoRecorder | Video Recorder")
	void StopRecording();

	UFUNCTION(BlueprintCallable, Category = "LBRuntimeVideoRecorder| Scene Shot")
	void SceneShot(const FString& FileName = "SceneShot");

	UFUNCTION(BlueprintPure, BlueprintCallable, Category = "LBRuntimeVideoRecorder| Utils")
	FString GetDateString(FString Format = "%Y.%m.%d-%H.%M.%S");

	UFUNCTION(BlueprintNativeEvent, Category = "LBRuntimeVideoRecorder| Scene Shot")
	FString GetSceneShotStoragePath();
	FString GetSceneShotStoragePath_Implementation()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RuntimeRecorder"), GetDateString("%Y%m%d"), TEXT("SceneShot"));
	}

	UFUNCTION(BlueprintNativeEvent, Category = "LBRuntimeVideoRecorder| Scene Shot")
	FString GetVideoStoragePath();
	FString GetVideoStoragePath_Implementation()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RuntimeRecorder"), GetDateString("%Y%m%d"), TEXT("Video"));
	}
protected:

private:
	// 当前实际的宽度和高度（从分辨率枚举派生）
	int32 CurrentWidth = 1920;
	int32 CurrentHeight = 1080;

	bool bIsRecording = false;
	float TimeAccumulator = 0.f;
	float FrameInterval = 1.f / 30.f;
	int64 FrameCounter = 0;
	FString CurrentVideoFilePath;

	// Encode 线程对象（逻辑）
	FLBRFFmpegEncodeThread* EncodeThread = nullptr;

	// UE 线程包装
	FRunnableThread* EncodeRunnable = nullptr;

	// 音频捕获
	TSharedPtr<LBSubmixCapture> AudioCapture;
private:
	// 获取指定分辨率对应的宽高
	FIntPoint GetResolutionFromEnum(ELBRVideoResolution Resolution) const;

	void InitRenderTarget();
	void CaptureFrameAsync();
	void CaptureAsync(
		UTextureRenderTarget2D* RenderTarget,
		float InGamma,
		float InExposure,
		TFunction<void(const TArray<FColor>&, int32, int32)> Callback);
	void ExecuteSceneShot(const FString& FileName);
};