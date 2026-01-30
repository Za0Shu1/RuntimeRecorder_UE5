// Fill out your copyright notice in the Description page of Project Settings.


#include "LBRuntimeRecorderSubsystem.h"
#include "LBRuntimeSceneCaptureActor.h"
#include "LBRRSceneShotCapture.h"
#include "EngineUtils.h"

void ULBRuntimeRecorderSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void ULBRuntimeRecorderSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void ULBRuntimeRecorderSubsystem::SceneShot(ALBRuntimeSceneCaptureActor* CaptureActor)
{
	if (CaptureActor)
	{
		// 直接使用前缀作为文件名
		FString FullPath = FPaths::Combine(CaptureActor->GetStorageDirectory(), CaptureActor->GetStorageFileNamePrefix() + TEXT(".png"));
		DoSceneShot(CaptureActor, FullPath);
	}
}

void ULBRuntimeRecorderSubsystem::SceneShotAll()
{
	int32 CaptureIndex = 0;
	for (TActorIterator<ALBRuntimeSceneCaptureActor> It(GetWorld()); It; ++It)
	{
		FString StorageDir = It->GetStorageDirectory();
		// 使用前缀加索引作为文件名，避免冲突
		FString FileName = It->GetStorageFileNamePrefix() + "_" + FString::FromInt(CaptureIndex++);
		FString FullPath = FPaths::Combine(StorageDir, FileName + TEXT(".png"));
		DoSceneShot(*It, FullPath);
	}
}

void ULBRuntimeRecorderSubsystem::DoSceneShot(ALBRuntimeSceneCaptureActor* CaptureActor, const FString& FilePath)
{
	if (!CaptureActor || !CaptureActor->RenderTarget) return;

	if (FilePath.IsEmpty()) return;

	// 提交渲染指令
	CaptureActor->SceneShot();

	LBRRSceneShotCapture::GetPixelsFromRenderTarget(
		CaptureActor->RenderTarget,
		[this, FilePath, CaptureActor](const TArray<FColor>& Pixels, int32 Width, int32 Height)
		{
			// 捕获完成回调，保存为PNG
			LBRRSceneShotCapture::SavePixelsToPNG(Pixels, Width, Height, FilePath);

			OnSceneShotCompleteDelegate.Broadcast(FilePath, true);
		},
		CaptureActor->Gamma,
		CaptureActor->Exposure
	);
}

