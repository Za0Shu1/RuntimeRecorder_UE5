// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ISubmixBufferListener.h"
#include "LBRTypes.h"
/**
 * 
 */

DECLARE_LOG_CATEGORY_EXTERN(LogLBSubmixCapture, Log, All);
DECLARE_DELEGATE_OneParam(FLBRAudioFrameDelegate, FLBRAudioFrame&&);

class LBSubmixCapture : public ISubmixBufferListener
{
public:
	LBSubmixCapture() {};
	virtual ~LBSubmixCapture() = default;

	bool Initialize();
	bool Uninitialize();

	// ISubmixBufferListener
	void OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock) override;
	const FString& GetListenerName() const override;
	// ~ ISubmixBufferListener

	FLBRAudioFrameDelegate OnAudioFrame;

private:
	int32 GetSamplesPerDurationSecs(float InSeconds) const;

private:
	TArray<float> RecordingBuffer;
	FCriticalSection CriticalSection;
	bool bInitialized = false;
	int32 CurrentSampleRate = 0;
	int32 CurrentNumChannels = 0;
	int64 AudioSampleCursor = 0;
};
