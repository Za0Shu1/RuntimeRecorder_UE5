// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "LBRuntimeSceneCaptureActor.generated.h"

UCLASS()
class LBRUNTIMERECORDER_API ALBRuntimeSceneCaptureActor : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ALBRuntimeSceneCaptureActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config")
	USceneCaptureComponent2D* CaptureComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Config")
	UTextureRenderTarget2D* RenderTarget;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	int32 RenderTargetWidth = 1920;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	int32 RenderTargetHeight = 1080;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	float Gamma = 2.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Config")
	float Exposure = 1.5f;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void OnConstruction(const FTransform& Transform) override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;


	// 存储路径
	UFUNCTION(BlueprintNativeEvent, Category = "Config")
	FString GetStorageDirectory();

	FString GetStorageDirectory_Implementation()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RuntimeRecorder"), TEXT("Screenshot"),GetDateString("%Y%m%d"));
	}

	// 文件名前缀
	UFUNCTION(BlueprintNativeEvent, Category = "Config")
	FString GetStorageFileNamePrefix();

	FString GetStorageFileNamePrefix_Implementation()
	{
		return GetDateString("%H.%M.%S");
	}

	UFUNCTION(BlueprintPure, BlueprintCallable, Category = "Config")
	FString GetDateString(FString Format = "%Y.%m.%d-%H.%M.%S");


	void SceneShot();

private:
	void InitRenderTarget();

};
