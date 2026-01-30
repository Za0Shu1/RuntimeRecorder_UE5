// Fill out your copyright notice in the Description page of Project Settings.


#include "LBRuntimeSceneCaptureActor.h"
#include "Kismet/KismetRenderingLibrary.h"

// Sets default values
ALBRuntimeSceneCaptureActor::ALBRuntimeSceneCaptureActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	CaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("SceneCapture"));
	CaptureComponent->SetupAttachment(RootComponent);

	// 默认不开启实时捕捉
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;

	// 设置CaptureSource为HDR模式
	CaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;

	// 优化后处理设置，保留暗部细节
	CaptureComponent->PostProcessSettings.bOverride_AutoExposureMethod = true;
	CaptureComponent->PostProcessSettings.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram; // 更稳定的直方图曝光
	CaptureComponent->PostProcessSettings.AutoExposureMinBrightness = 0.01f; // 更低的暗部兜底，避免死黑
	CaptureComponent->PostProcessSettings.AutoExposureMaxBrightness = 4.0f;   // 更高的亮部上限，保留高光
	CaptureComponent->PostProcessSettings.AutoExposureSpeedUp = 5.0f;        // 曝光提升速度（加快临时开启时的响应）
	CaptureComponent->PostProcessSettings.AutoExposureSpeedDown = 2.0f;      // 曝光降低速度
}

// Called when the game starts or when spawned
void ALBRuntimeSceneCaptureActor::BeginPlay()
{
	Super::BeginPlay();

}

void ALBRuntimeSceneCaptureActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	InitRenderTarget();
}

// Called every frame
void ALBRuntimeSceneCaptureActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

FString ALBRuntimeSceneCaptureActor::GetDateString(FString Format)
{
	return FDateTime::Now().ToString(*Format);
}

void ALBRuntimeSceneCaptureActor::SceneShot()
{
	if (!CaptureComponent || !RenderTarget)
	{
		return;
	}

	// 临时开启持续捕获（触发曝光计算）
	CaptureComponent->bCaptureEveryFrame = true;
	// 强制刷新组件，确保曝光逻辑启动
	CaptureComponent->MarkRenderStateDirty();

	// 强制渲染一帧（关键：让曝光完成计算）
	// 额外刷新渲染目标（兜底，确保画面正确）
	UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), RenderTarget, FLinearColor::Black);
	CaptureComponent->CaptureScene(); 

	// 立即关闭持续捕获（恢复性能）
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
}

void ALBRuntimeSceneCaptureActor::InitRenderTarget()
{
	if (RenderTarget) return;

	RenderTarget = NewObject<UTextureRenderTarget2D>(this);
	// 设置为HDR格式，保留暗部和亮部细节（解决死黑核心）
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA16f;
	RenderTarget->InitAutoFormat(RenderTargetWidth, RenderTargetHeight);

	RenderTarget->TargetGamma = Gamma;

	RenderTarget->ClearColor = FLinearColor::Black;

	RenderTarget->UpdateResourceImmediate(true);
	CaptureComponent->TextureTarget = RenderTarget;
}

