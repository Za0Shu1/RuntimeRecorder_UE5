// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LBRFFmpegEncodeThread.h"
#include "LBRuntimeVideoRecorderActor.generated.h"

class FLBRSimpleAsyncCapture
{
public:
    static void CaptureAsync(
        UTextureRenderTarget2D* RenderTarget,
        float InGamma,
        float InExposure,
        TFunction<void(const TArray<FColor>&, int32, int32)> Callback);
    
};

UCLASS()
class LBRUNTIMERECORDER_API ALBRuntimeVideoRecorderActor : public AActor
{
	GENERATED_BODY()
	
public:
    ALBRuntimeVideoRecorderActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config")
    USceneCaptureComponent2D* CaptureComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config")
    UTextureRenderTarget2D* RenderTarget;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    int32 VideoWidth = 1920;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    int32 VideoHeight = 1080;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    float Gamma = 2.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    float Exposure = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
    float CaptureFPS = 30.f;


    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "LBRuntimeVideoRecorder")
	void CaptureOneFrame();

    UFUNCTION(BlueprintCallable, Category = "LBRuntimeVideoRecorder")
	void CaptureOneFrameAsync();

    UFUNCTION(BlueprintCallable, Category = "LBRuntimeVideoRecorder")
    void StartRecording();

    UFUNCTION(BlueprintCallable, Category = "LBRuntimeVideoRecorder")
    void StopRecording();

private:
	bool bIsRecording = false;
	float TimeAccumulator = 0.f;
    float FrameInterval = 1.f / 30.f;

private:
    void InitRenderTarget();
};