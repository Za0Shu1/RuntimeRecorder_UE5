// Fill out your copyright notice in the Description page of Project Settings.


#include "LBSubmixCapture.h"
#include "AudioDevice.h"
#include "SampleBuffer.h"

DEFINE_LOG_CATEGORY(LogLBSubmixCapture);

bool LBSubmixCapture::Initialize()
{
	FScopeLock Lock(&CriticalSection);
	if (bInitialized)
	{
		return true;
	}

	if (!GEngine)
	{
		bInitialized = false;
		return false;
	}

	//FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();  // AudioData的数据可能全是0.0
	FAudioDeviceHandle AudioDevice = GEngine->GetActiveAudioDevice();
	if (!AudioDevice)
	{
		UE_LOG(LogLBSubmixCapture, Warning, TEXT("No audio device"));
		bInitialized = false;
		return false;
	}

	AudioDevice->RegisterSubmixBufferListener(AsShared(), AudioDevice->GetMainSubmixObject());

	if (GConfig)
	{
		GConfig->SetFloat(TEXT("Audio"), TEXT("UnfocusedVolumeMultiplier"), 1.f, GEngineIni);
	}

	bInitialized = true;
	return true;
}

bool LBSubmixCapture::Uninitialize()
{
	FScopeLock Lock(&CriticalSection);
	if (!bInitialized)
	{
		return true;
	}

	if (GEngine)
	{
		//FAudioDeviceHandle AudioDevice = GEngine->GetMainAudioDevice();
		FAudioDeviceHandle AudioDevice = GEngine->GetActiveAudioDevice();
		if (AudioDevice)
		{
			AudioDevice->UnregisterSubmixBufferListener(AsShared(), AudioDevice->GetMainSubmixObject());
		}
	}

	RecordingBuffer.Empty();
	bInitialized = false;
	return true;
}

void LBSubmixCapture::OnNewSubmixBuffer(const USoundSubmix* OwningSubmix, float* AudioData, int32 NumSamples, int32 NumChannels, const int32 SampleRate, double AudioClock)
{
	FScopeLock Lock(&CriticalSection);

	if (!bInitialized)
	{
		return;
	}

	CurrentSampleRate = SampleRate;
	CurrentNumChannels = NumChannels;

	Audio::TSampleBuffer<int16> Buffer(AudioData, NumSamples, NumChannels, SampleRate);

	RecordingBuffer.Append(AudioData, NumSamples);

	const float ChunkDurationSecs = 0.01f; // 10ms
	const int32 SamplesPer10Ms = GetSamplesPerDurationSecs(ChunkDurationSecs);

	// 发送10ms的数据
	while (RecordingBuffer.Num() > SamplesPer10Ms)
	{
		// Extract a 10ms chunk of samples from recording buffer
		FLBRAudioFrame Frame;
		Frame.NumChannels = CurrentNumChannels;
		Frame.SampleRate = CurrentSampleRate;
		Frame.PTS = AudioSampleCursor;

		Frame.Samples.Append(
			RecordingBuffer.GetData(),
			SamplesPer10Ms
		);

		AudioSampleCursor += SamplesPer10Ms / CurrentNumChannels;

		// Remove 10ms of samples from the recording buffer now it is submitted
		RecordingBuffer.RemoveAt(0, SamplesPer10Ms, EAllowShrinking::No);

		if (OnAudioFrame.IsBound())
		{
			OnAudioFrame.Execute(MoveTemp(Frame));
		}
	}

}

const FString& LBSubmixCapture::GetListenerName() const
{
	static const FString ListenerName = TEXT("LBSubmixCapture");
	return ListenerName;
}

int32 LBSubmixCapture::GetSamplesPerDurationSecs(float InSeconds) const
{
	if (CurrentSampleRate <= 0 || CurrentNumChannels <= 0)
	{
		return 0;
	}

	const int32 SamplesPerSecond = CurrentSampleRate * CurrentNumChannels;
	return FMath::RoundToInt(SamplesPerSecond * InSeconds);
}
