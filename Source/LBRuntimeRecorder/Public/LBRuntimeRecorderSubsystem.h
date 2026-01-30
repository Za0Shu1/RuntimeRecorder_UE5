// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LBRuntimeRecorderSubsystem.generated.h"

/**
 *
 */

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSceneShotCompleteSignature, const FString&, FilePath, bool, bSuccess);

class ALBRuntimeSceneCaptureActor;

UCLASS()
class LBRUNTIMERECORDER_API ULBRuntimeRecorderSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "LBRuntimeRecorder | Screenshot")
	void SceneShot(ALBRuntimeSceneCaptureActor* CaptureActor);

	UFUNCTION(BlueprintCallable, Category = "LBRuntimeRecorder | Screenshot")
	void SceneShotAll();

	UPROPERTY(BlueprintAssignable, Category = "LBRuntimeRecorder | Screenshot")
	FOnSceneShotCompleteSignature OnSceneShotCompleteDelegate;

private:
	void DoSceneShot(ALBRuntimeSceneCaptureActor* CaptureActor, const FString& FilePath);
};
