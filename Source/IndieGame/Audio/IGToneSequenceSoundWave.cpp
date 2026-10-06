#include "Audio/IGToneSequenceSoundWave.h"

#include "Narrative/IGRecordingSubsystem.h"

namespace IGToneSequence
{
	constexpr int32 SampleRateHz = 48000;
	constexpr float TwoPi = 2.0f * UE_PI;

	// Equal-tempered pitches used by the composed cues.
	constexpr float NoteC3 = 130.81f;
	constexpr float NoteG2 = 98.00f;
	constexpr float NoteA2 = 110.00f;
	constexpr float NoteF3 = 174.61f;
	constexpr float NoteC4 = 261.63f;
	constexpr float NoteD4 = 293.66f;
	constexpr float NoteE4 = 329.63f;
	constexpr float NoteG4 = 392.00f;
	constexpr float NoteA4 = 440.00f;
	constexpr float NoteC5 = 523.25f;
	constexpr float NoteD5 = 587.33f;
	constexpr float NoteE5 = 659.26f;

	float HashToSigned(const uint32 Value)
	{
		uint32 Hash = Value * 2654435761u;
		Hash ^= Hash >> 16;
		Hash *= 2246822519u;
		Hash ^= Hash >> 13;
		Hash *= 3266489917u;
		Hash ^= Hash >> 16;
		return (static_cast<float>(Hash) / 2147483648.0f) - 1.0f;
	}

	/** xorshift32. 잡음 파형의 샘플별 난수. 상태는 음마다 따로 든다. */
	float NextWhite(uint32& State)
	{
		State ^= State << 13;
		State ^= State >> 17;
		State ^= State << 5;
		return (static_cast<float>(State) / 2147483648.0f) - 1.0f;
	}

	UIGToneSequenceSoundWave* NewWave(UObject* Outer, const TCHAR* BaseName)
	{
		return NewObject<UIGToneSequenceSoundWave>(
			Outer,
			MakeUniqueObjectName(Outer, UIGToneSequenceSoundWave::StaticClass(), BaseName));
	}
}

UIGToneSequenceSoundWave::UIGToneSequenceSoundWave(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bProcedural = true;
	bCanProcessAsync = true;
	bLooping = false;
	NumChannels = 1;
	SetSampleRate(IGToneSequence::SampleRateHz);
	SampleByteSize = sizeof(int16);
	Volume = 1.0f;
}

void UIGToneSequenceSoundWave::ConfigureNotes(
	TArray<FIGToneNote>&& InNotes,
	const bool bInLooping,
	const float LoopSeconds)
{
	Notes = MoveTemp(InNotes);
	GeneratedSampleCount = 0;
	// 상태 파형의 작업 기억은 여기서, 게임 스레드에서 미리 잡는다. 렌더 스레드는
	// 채우기만 한다.
	NoteStates.Reset();
	NoteStates.SetNum(Notes.Num());
	for (int32 NoteIndex = 0; NoteIndex < Notes.Num(); ++NoteIndex)
	{
		FNoteRenderState& State = NoteStates[NoteIndex];
		State.NoiseState = 0x9E3779B9u ^ (static_cast<uint32>(NoteIndex + 1) * 2654435761u);
		if (Notes[NoteIndex].Waveform == EIGToneWaveform::Pluck)
		{
			const float Frequency = FMath::Max(Notes[NoteIndex].FrequencyHz, 40.0f);
			State.Delay.SetNumZeroed(FMath::Max(2, FMath::RoundToInt(IGToneSequence::SampleRateHz / Frequency)));
		}
	}

	float LastNoteEndSeconds = 0.0f;
	for (const FIGToneNote& Note : Notes)
	{
		LastNoteEndSeconds = FMath::Max(
			LastNoteEndSeconds,
			Note.StartSeconds + Note.DurationSeconds);
	}

	bLooping = bInLooping;
	if (bInLooping)
	{
		const float SafeLoopSeconds = FMath::Max(LoopSeconds, LastNoteEndSeconds);
		LoopSampleCount = static_cast<int64>(SafeLoopSeconds * IGToneSequence::SampleRateHz);
		TotalSampleCount = 0;
		Duration = INDEFINITELY_LOOPING_DURATION;
		VirtualizationMode = EVirtualizationMode::PlayWhenSilent;
	}
	else
	{
		// A short silent tail protects the release envelope from hard cutoff.
		const float TotalSeconds = LastNoteEndSeconds + 0.05f;
		LoopSampleCount = 0;
		TotalSampleCount = static_cast<int64>(TotalSeconds * IGToneSequence::SampleRateHz);
		Duration = TotalSeconds;
	}
}

void UIGToneSequenceSoundWave::ConfigurePitchWow(
	const float DepthRatio,
	const float RateHz)
{
	PitchWowDepthRatio = FMath::Clamp(DepthRatio, 0.0f, 0.02f);
	PitchWowRateHz = FMath::Clamp(RateHz, 0.0f, 4.0f);
}

void UIGToneSequenceSoundWave::ConfigureRoomTail(
	const float DelaySeconds,
	const float Feedback,
	const float Damping,
	const float Mix)
{
	TailDelaySamples = FMath::Clamp(
		FMath::RoundToInt(DelaySeconds * IGToneSequence::SampleRateHz), 8, IGToneSequence::SampleRateHz / 4);
	TailFeedback = FMath::Clamp(Feedback, 0.0f, 0.85f);
	TailDamping = FMath::Clamp(Damping, 0.02f, 1.0f);
	TailMix = FMath::Clamp(Mix, 0.0f, 1.0f);
	TailBuffer.SetNumZeroed(TailDelaySamples);
	TailIndex = 0;
	TailLow = 0.0f;
	if (!bLooping && TailMix > 0.0f && TailFeedback > 0.0f)
	{
		// 꼬리가 60dB 죽을 때까지 유한 파형을 늘린다. 안 늘리면 울림이 잘린다.
		const float Repeats = FMath::Log2(0.001f) / FMath::Log2(TailFeedback);
		const float TailSeconds = FMath::Clamp(Repeats * DelaySeconds, 0.0f, 3.0f);
		TotalSampleCount += static_cast<int64>(TailSeconds * IGToneSequence::SampleRateHz);
		Duration += TailSeconds;
	}
}

float UIGToneSequenceSoundWave::EvaluateWaveform(
	const EIGToneWaveform Waveform,
	const float FrequencyHz,
	const double NoteTimeSeconds)
{
	const double CyclesExact = NoteTimeSeconds * FrequencyHz;
	const float Phase01 = static_cast<float>(CyclesExact - FMath::FloorToDouble(CyclesExact));
	const float Radians = IGToneSequence::TwoPi * Phase01;

	switch (Waveform)
	{
	case EIGToneWaveform::Sine:
		return FMath::Sin(Radians);

	case EIGToneWaveform::SoftSquare:
		return 0.72f * FMath::Sin(Radians)
			+ 0.24f * FMath::Sin(3.0f * Radians)
			+ 0.10f * FMath::Sin(5.0f * Radians);

	case EIGToneWaveform::Triangle:
		return 2.0f * FMath::Abs(2.0f * (Phase01 - FMath::FloorToFloat(Phase01 + 0.5f))) - 1.0f;

	case EIGToneWaveform::ValueNoise:
	{
		// Interpolated hash noise; FrequencyHz sets the effective bandwidth.
		const double NoiseCursor = NoteTimeSeconds * FMath::Max(40.0f, FrequencyHz);
		const uint32 Cell = static_cast<uint32>(FMath::FloorToDouble(NoiseCursor));
		const float CellFraction = static_cast<float>(NoiseCursor - FMath::FloorToDouble(NoiseCursor));
		const float Smooth = CellFraction * CellFraction * (3.0f - 2.0f * CellFraction);
		return FMath::Lerp(
			IGToneSequence::HashToSigned(Cell),
			IGToneSequence::HashToSigned(Cell + 1u),
			Smooth);
	}

	case EIGToneWaveform::WhiteNoise:
	{
		// 샘플 하나마다 다른 해시. 상태 없이 시간만으로 같은 잡음이 다시 나온다.
		const uint32 Sample = static_cast<uint32>(NoteTimeSeconds * IGToneSequence::SampleRateHz);
		return IGToneSequence::HashToSigned(Sample * 7u + 3u);
	}

	case EIGToneWaveform::Sub:
	{
		// 1.8배 밀어 넣은 사인의 tanh. 최대치는 정확히 1이다.
		constexpr float Drive = 1.8f;
		constexpr float Normalize = 1.0f / 0.9468f; // tanh(1.8)
		return FMath::Tanh(Drive * FMath::Sin(Radians)) * Normalize;
	}

	case EIGToneWaveform::Crackle:
	{
		// 초당 Frequency개의 칸. 칸마다 해시로 켜질지 정하고, 켜진 칸은 앞쪽에서
		// 잡음이 터졌다가 세제곱으로 꺼진다.
		const double Cursor = NoteTimeSeconds * FMath::Max(1.0f, FrequencyHz);
		const uint32 Cell = static_cast<uint32>(FMath::FloorToDouble(Cursor));
		const float Gate = IGToneSequence::HashToSigned(Cell * 13u + 5u);
		if (Gate < 0.25f)
		{
			return 0.0f;
		}
		const float Fraction = static_cast<float>(Cursor - FMath::FloorToDouble(Cursor));
		const float Decay = FMath::Cube(1.0f - Fraction);
		const uint32 Sample = static_cast<uint32>(NoteTimeSeconds * IGToneSequence::SampleRateHz);
		return Decay * IGToneSequence::HashToSigned(Sample * 11u + Cell);
	}

	case EIGToneWaveform::Growl:
		// 상태 없는 FM. Resonance는 상태 파형 경로에서 읽으므로 여기서는 깊이 3.
		return FMath::Sin(Radians + 3.0f * FMath::Sin(Radians * 1.47f));

	default:
		return 0.0f;
	}
}

float UIGToneSequenceSoundWave::EvaluateStatefulWaveform(
	const FIGToneNote& Note,
	FNoteRenderState& State,
	const double NoteTimeSeconds,
	const int32 NoteIndex)
{
	// 루프가 돌아 음이 처음부터 다시 시작하면 기억도 비운다.
	if (NoteTimeSeconds < State.LastNoteTime)
	{
		State.FilterLow = 0.0f;
		State.FilterBand = 0.0f;
		State.DelayIndex = 0;
		State.NoiseState = 0x9E3779B9u ^ (static_cast<uint32>(NoteIndex + 1) * 2654435761u);
		for (float& Sample : State.Delay)
		{
			Sample = 0.0f;
		}
	}
	const bool bFirstSample = State.LastNoteTime < 0.0 || NoteTimeSeconds < State.LastNoteTime;
	State.LastNoteTime = NoteTimeSeconds;
	const float Resonance = FMath::Clamp(Note.Resonance, 0.0f, 1.0f);

	switch (Note.Waveform)
	{
	case EIGToneWaveform::BandNoise:
	{
		// 체임벌린 상태변수 필터의 대역 출력. 중심은 7kHz 아래로 묶어야 안정하다.
		const float Center = FMath::Clamp(Note.FrequencyHz, 30.0f, 7000.0f);
		const float Q = FMath::Lerp(2.0f, 40.0f, Resonance);
		const float F = 2.0f * FMath::Sin(UE_PI * Center / IGToneSequence::SampleRateHz);
		const float White = IGToneSequence::NextWhite(State.NoiseState);
		State.FilterLow += F * State.FilterBand;
		const float High = White - State.FilterLow - State.FilterBand / Q;
		State.FilterBand += F * High;
		// Q가 클수록 출력이 커진다. 1.2/√Q로 눌러 대개 ±1 안에 두고, 넘치면 자른다.
		return FMath::Clamp(State.FilterBand * 1.2f / FMath::Sqrt(Q), -1.0f, 1.0f);
	}

	case EIGToneWaveform::Pluck:
	{
		if (State.Delay.Num() < 2)
		{
			return 0.0f;
		}
		if (bFirstSample)
		{
			// 줄을 튕긴다: 지연선을 잡음으로 채운다.
			for (float& Sample : State.Delay)
			{
				Sample = IGToneSequence::NextWhite(State.NoiseState);
			}
		}
		const int32 Length = State.Delay.Num();
		const int32 NextIndex = (State.DelayIndex + 1) % Length;
		const float Current = State.Delay[State.DelayIndex];
		// 이웃 둘의 평균이 저역 필터, 감쇠가 울림 길이. 둘 다 1 미만이라 커지지 않는다.
		const float Decay = FMath::Lerp(0.90f, 0.998f, Resonance);
		State.Delay[State.DelayIndex] = (Current + State.Delay[NextIndex]) * 0.5f * Decay;
		State.DelayIndex = NextIndex;
		return Current;
	}

	case EIGToneWaveform::Growl:
	{
		// 반송파에 1.47배 변조파. 깊이는 Resonance. 숨 섞인 소리로 잡음을 조금 얹는다.
		const double Cycles = NoteTimeSeconds * Note.FrequencyHz;
		const float Phase = static_cast<float>(Cycles - FMath::FloorToDouble(Cycles));
		const float Radians = IGToneSequence::TwoPi * Phase;
		const double ModCycles = Cycles * 1.47;
		const float ModPhase = static_cast<float>(ModCycles - FMath::FloorToDouble(ModCycles));
		const float Index = Resonance * 7.0f;
		const float Voice = FMath::Sin(Radians + Index * FMath::Sin(IGToneSequence::TwoPi * ModPhase));
		const float Breath = IGToneSequence::NextWhite(State.NoiseState) * 0.12f;
		return FMath::Clamp(Voice * 0.88f + Breath, -1.0f, 1.0f);
	}

	default:
		return EvaluateWaveform(Note.Waveform, Note.FrequencyHz, NoteTimeSeconds);
	}
}

float UIGToneSequenceSoundWave::EvaluateEnvelope(const FIGToneNote& Note, const float NoteProgress01)
{
	const float AttackFraction = FMath::Clamp(Note.AttackFraction, 0.001f, 0.9f);
	if (NoteProgress01 <= AttackFraction)
	{
		const float Attack = NoteProgress01 / AttackFraction;
		return Attack * Attack * (3.0f - 2.0f * Attack);
	}

	const float ReleaseProgress =
		(NoteProgress01 - AttackFraction) / FMath::Max(1.0f - AttackFraction, 0.001f);
	return FMath::Pow(
		FMath::Max(0.0f, 1.0f - ReleaseProgress),
		FMath::Max(0.25f, Note.ReleasePower));
}

int32 UIGToneSequenceSoundWave::OnGeneratePCMAudio(TArray<uint8>& OutAudio, const int32 NumSamples)
{
	if (NumSamples <= 0)
	{
		OutAudio.Reset();
		return 0;
	}

	OutAudio.SetNumUninitialized(NumSamples * sizeof(int16), EAllowShrinking::No);
	int16* const OutputSamples = reinterpret_cast<int16*>(OutAudio.GetData());

	// The schedule stays tiny (a few dozen notes), so scanning every note per
	// sample is cheaper and simpler than maintaining sorted active cursors.
	for (int32 OutputIndex = 0; OutputIndex < NumSamples; ++OutputIndex)
	{
		const uint64 AbsoluteSample = GeneratedSampleCount + static_cast<uint64>(OutputIndex);

		double PatternSeconds;
		if (LoopSampleCount > 0)
		{
			PatternSeconds =
				static_cast<double>(AbsoluteSample % static_cast<uint64>(LoopSampleCount)) /
				IGToneSequence::SampleRateHz;
		}
		else
		{
			PatternSeconds = static_cast<double>(AbsoluteSample) / IGToneSequence::SampleRateHz;
			if (TotalSampleCount > 0 && AbsoluteSample >= static_cast<uint64>(TotalSampleCount))
			{
				OutputSamples[OutputIndex] = 0;
				continue;
			}
		}

		float Mixed = 0.0f;
		for (int32 NoteIndex = 0; NoteIndex < Notes.Num(); ++NoteIndex)
		{
			const FIGToneNote& Note = Notes[NoteIndex];
			const double NoteTime = PatternSeconds - Note.StartSeconds;
			if (NoteTime < 0.0 || NoteTime >= Note.DurationSeconds)
			{
				continue;
			}

			const float Progress = static_cast<float>(NoteTime / Note.DurationSeconds);
			double WaveTime = NoteTime;
			if (PitchWowDepthRatio > 0.0f && PitchWowRateHz > 0.0f)
			{
				// Integrate 1 + depth*sin(wt) so wow changes instantaneous pitch
				// without the discontinuities caused by multiplying phase directly.
				const double AngularRate =
					IGToneSequence::TwoPi * PitchWowRateHz;
				const double PatternOffset =
					PitchWowDepthRatio / AngularRate
					* (1.0 - FMath::Cos(AngularRate * PatternSeconds));
				const double NoteStartOffset =
					PitchWowDepthRatio / AngularRate
					* (1.0 - FMath::Cos(AngularRate * Note.StartSeconds));
				WaveTime += PatternOffset - NoteStartOffset;
			}
			const bool bStateful =
				Note.Waveform == EIGToneWaveform::BandNoise
				|| Note.Waveform == EIGToneWaveform::Pluck
				|| Note.Waveform == EIGToneWaveform::Growl;
			const float Sample = bStateful && NoteStates.IsValidIndex(NoteIndex)
				? EvaluateStatefulWaveform(Note, NoteStates[NoteIndex], WaveTime, NoteIndex)
				: EvaluateWaveform(Note.Waveform, Note.FrequencyHz, WaveTime);
			Mixed += Note.Amplitude * EvaluateEnvelope(Note, Progress) * Sample;
		}

		// 방 울림. 지연선 하나에 되먹임과 저역 필터. 음의 합 위에 Mix만큼 얹는다.
		if (TailMix > 0.0f && TailBuffer.Num() > 0)
		{
			const float Delayed = TailBuffer[TailIndex];
			TailLow += (Delayed - TailLow) * TailDamping;
			TailBuffer[TailIndex] = Mixed + TailFeedback * TailLow;
			TailIndex = (TailIndex + 1) % TailBuffer.Num();
			Mixed += TailMix * TailLow;
		}

		OutputSamples[OutputIndex] = static_cast<int16>(
			FMath::RoundToInt(FMath::Clamp(Mixed, -1.0f, 1.0f) * 32767.0f));
	}

	GeneratedSampleCount += static_cast<uint64>(NumSamples);
	return NumSamples;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateDoorChime(UObject* Outer)
{
	using namespace IGToneSequence;
	UIGToneSequenceSoundWave* Wave = NewWave(Outer, TEXT("IGDoorChime"));
	TArray<FIGToneNote> ChimeNotes;
	ChimeNotes.Add({0.00f, 0.55f, NoteE5, 0.230f, 0.008f, 2.6f, EIGToneWaveform::Sine});
	ChimeNotes.Add({0.00f, 0.45f, NoteE5 * 2.0f, 0.050f, 0.008f, 3.0f, EIGToneWaveform::Sine});
	ChimeNotes.Add({0.42f, 0.75f, NoteC5, 0.230f, 0.008f, 2.6f, EIGToneWaveform::Sine});
	ChimeNotes.Add({0.42f, 0.60f, NoteC5 * 2.0f, 0.050f, 0.008f, 3.0f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(ChimeNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateDoorCreak(UObject* Outer, const bool bClosing)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGDoorCreak"));
	TArray<FIGToneNote> CreakNotes;
	// 열릴 때는 내려가고 닫힐 때는 같은 경첩이 거꾸로 운다.
	const float OpenFrequencies[] = {338.0f, 296.0f, 318.0f, 262.0f, 228.0f, 189.0f, 161.0f};
	const float CloseFrequencies[] = {161.0f, 189.0f, 228.0f, 262.0f, 318.0f, 296.0f, 338.0f};
	float StartSeconds = 0.0f;
	for (int32 StepIndex = 0; StepIndex < 7; ++StepIndex)
	{
		const float Frequency = bClosing ? CloseFrequencies[StepIndex] : OpenFrequencies[StepIndex];
		CreakNotes.Add({StartSeconds, 0.12f, Frequency, 0.048f, 0.25f, 1.2f, EIGToneWaveform::SoftSquare});
		CreakNotes.Add({StartSeconds, 0.12f, Frequency * 3.1f, 0.016f, 0.25f, 1.2f, EIGToneWaveform::ValueNoise});
		StartSeconds += 0.075f;
	}
	Wave->ConfigureNotes(MoveTemp(CreakNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePickupRustle(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGPickupRustle"));
	TArray<FIGToneNote> Notes;
	// 손가락이 닿는 스침 둘, 들어 올리며 나는 낮은 툭 하나.
	Notes.Add({0.000f, 0.070f, 2400.0f, 0.110f, 0.20f, 1.6f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.050f, 0.090f, 3100.0f, 0.090f, 0.30f, 1.8f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.075f, 0.060f, 150.0f, 0.160f, 0.02f, 3.0f, EIGToneWaveform::Sine});
	Notes.Add({0.075f, 0.040f, 900.0f, 0.070f, 0.05f, 2.2f, EIGToneWaveform::ValueNoise});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePackingTapeRip(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGPackingTapeRip"));
	TArray<FIGToneNote> Notes;
	// 접착면이 떨어지며 촘촘히 튀는 알갱이와 밝은 마찰이 0.3초 남짓 이어진다.
	// 끝에서 테이프가 날개를 떠나는 딸깍, 날개가 서는 골판지의 툭. 손 소리라
	// 저역은 짧게 끊는다 — 위층에서 끌리는 상자와 섞이면 안 된다.
	Notes.Add({0.000f, 0.290f, 2400.0f, 0.110f, 0.06f, 1.3f, EIGToneWaveform::Crackle});
	Notes.Add({0.010f, 0.280f, 2600.0f, 0.060f, 0.08f, 1.4f, EIGToneWaveform::BandNoise, 0.25f});
	Notes.Add({0.290f, 0.012f, 5200.0f, 0.060f, 0.05f, 1.5f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.295f, 0.120f, 420.0f, 0.050f, 0.03f, 2.4f, EIGToneWaveform::BandNoise, 0.35f});
	Notes.Add({0.295f, 0.090f, 110.0f, 0.045f, 0.02f, 3.0f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateToolRollClink(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGToolRollClink"));
	TArray<FIGToneNote> Notes;
	// 작은 강철 공구라 울림이 짧고 배음이 서로 어긋난다. 한 번 닿고, 0.1초
	// 뒤 옆의 것이 가볍게 한 번 더 닿는다.
	Notes.Add({0.000f, 0.008f, 5600.0f, 0.070f, 0.05f, 1.5f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.000f, 0.160f, 3170.0f, 0.050f, 0.01f, 2.6f, EIGToneWaveform::Sine});
	Notes.Add({0.000f, 0.120f, 4930.0f, 0.030f, 0.01f, 3.0f, EIGToneWaveform::Sine});
	Notes.Add({0.000f, 0.090f, 1760.0f, 0.020f, 0.01f, 2.8f, EIGToneWaveform::Triangle});
	Notes.Add({0.095f, 0.006f, 5600.0f, 0.040f, 0.05f, 1.5f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.095f, 0.110f, 2710.0f, 0.032f, 0.01f, 2.8f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateSwitchClick(UObject* Outer, const bool bOn)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGSwitchClick"));
	TArray<FIGToneNote> Notes;
	if (bOn)
	{
		// 플라스틱 슬라이드가 걸리는 짧은 딸깍. 밝은 클릭 뒤 작은 몸통 울림.
		Notes.Add({0.000f, 0.012f, 4200.0f, 0.240f, 0.05f, 1.5f, EIGToneWaveform::ValueNoise});
		Notes.Add({0.004f, 0.045f, 1900.0f, 0.120f, 0.05f, 2.6f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.006f, 0.060f, 260.0f, 0.090f, 0.05f, 2.8f, EIGToneWaveform::Sine});
	}
	else
	{
		// 끌 때는 아래로 미는 둔탁한 톡. 몸통이 조금 더 길게 운다.
		Notes.Add({0.000f, 0.014f, 2600.0f, 0.180f, 0.05f, 1.5f, EIGToneWaveform::ValueNoise});
		Notes.Add({0.004f, 0.050f, 1200.0f, 0.100f, 0.05f, 2.6f, EIGToneWaveform::SoftSquare});
		Notes.Add({0.006f, 0.080f, 190.0f, 0.110f, 0.05f, 2.8f, EIGToneWaveform::Sine});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateKickedPropKnock(
	UObject* Outer,
	const bool bHollow)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGKickedPropKnock"));
	TArray<FIGToneNote> Notes;
	if (bHollow)
	{
		// 빈 페트병. 얇은 플라스틱이 딱 하고, 속 빈 통이 짧게 운다.
		Notes.Add({0.000f, 0.008f, 3200.0f, 0.160f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
		Notes.Add({0.002f, 0.060f, 1500.0f, 0.120f, 0.05f, 2.4f, EIGToneWaveform::BandNoise, 0.40f});
		Notes.Add({0.004f, 0.090f, 540.0f, 0.090f, 0.02f, 3.0f, EIGToneWaveform::Sine});
	}
	else
	{
		// 실내화 밑창이나 골판지 모서리. 둔한 톡 하나, 울림은 거의 없다.
		Notes.Add({0.000f, 0.005f, 3000.0f, 0.120f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
		Notes.Add({0.001f, 0.070f, 700.0f, 0.160f, 0.04f, 2.4f, EIGToneWaveform::BandNoise, 0.35f});
		Notes.Add({0.000f, 0.060f, 110.0f, 0.100f, 0.01f, 3.2f, EIGToneWaveform::Sub});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateDoorThud(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGDoorThud"));
	TArray<FIGToneNote> ThudNotes;
	// 문짝이 문틀을 만나는 저역, 판이 우는 대역 잡음, 걸쇠가 물리는 금속 딸깍.
	ThudNotes.Add({0.00f, 0.004f, 3200.0f, 0.140f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	ThudNotes.Add({0.00f, 0.13f, 74.0f, 0.300f, 0.010f, 3.0f, EIGToneWaveform::Sub});
	ThudNotes.Add({0.00f, 0.09f, 520.0f, 0.110f, 0.05f, 2.0f, EIGToneWaveform::BandNoise, 0.30f});
	ThudNotes.Add({0.03f, 0.14f, 1240.0f, 0.070f, 0.01f, 2.4f, EIGToneWaveform::Pluck, 0.30f});
	Wave->ConfigureNotes(MoveTemp(ThudNotes), false);
	Wave->ConfigureRoomTail(0.016f, 0.32f, 0.30f, 0.16f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateLockedRattle(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGLockedRattle"));
	TArray<FIGToneNote> RattleNotes;
	// 손잡이가 걸쇠에 걸려 세 번 덜컹. 금속은 튕긴 줄, 문짝은 저역.
	RattleNotes.Add({0.00f, 0.05f, 950.0f, 0.100f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	RattleNotes.Add({0.00f, 0.09f, 2100.0f, 0.090f, 0.01f, 2.0f, EIGToneWaveform::Pluck, 0.30f});
	RattleNotes.Add({0.09f, 0.05f, 900.0f, 0.110f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	RattleNotes.Add({0.09f, 0.09f, 1900.0f, 0.090f, 0.01f, 2.0f, EIGToneWaveform::Pluck, 0.30f});
	RattleNotes.Add({0.18f, 0.07f, 870.0f, 0.100f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	RattleNotes.Add({0.18f, 0.09f, 92.0f, 0.180f, 0.02f, 2.5f, EIGToneWaveform::Sub});
	RattleNotes.Add({0.18f, 0.10f, 1700.0f, 0.100f, 0.01f, 2.0f, EIGToneWaveform::Pluck, 0.34f});
	Wave->ConfigureNotes(MoveTemp(RattleNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateHeldDoorPush(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGHeldDoorPush"));
	TArray<FIGToneNote> PushNotes;
	// 푸시바가 끝까지 들어간다. 걸쇠는 풀렸다.
	PushNotes.Add({0.000f, 0.014f, 2600.0f, 0.200f, 0.05f, 1.5f, EIGToneWaveform::ValueNoise});
	PushNotes.Add({0.000f, 0.040f, 1400.0f, 0.090f, 0.05f, 2.4f, EIGToneWaveform::SoftSquare});
	// 그런데 문짝이 몇 밀리 가다 둔하게 선다. 유리판만 틀 안에서 떤다.
	PushNotes.Add({0.060f, 0.160f, 68.0f, 0.260f, 0.02f, 3.0f, EIGToneWaveform::Sub});
	PushNotes.Add({0.060f, 0.100f, 460.0f, 0.090f, 0.05f, 2.2f, EIGToneWaveform::BandNoise, 0.30f});
	PushNotes.Add({0.065f, 0.220f, 3100.0f, 0.035f, 0.02f, 2.0f, EIGToneWaveform::BandNoise, 0.60f});
	// 한 번 더 민다. 같은 자리에서 조금 약하게 선다.
	PushNotes.Add({0.340f, 0.140f, 64.0f, 0.200f, 0.02f, 3.0f, EIGToneWaveform::Sub});
	PushNotes.Add({0.340f, 0.080f, 440.0f, 0.070f, 0.05f, 2.2f, EIGToneWaveform::BandNoise, 0.30f});
	PushNotes.Add({0.345f, 0.180f, 3000.0f, 0.028f, 0.02f, 2.0f, EIGToneWaveform::BandNoise, 0.60f});
	Wave->ConfigureNotes(MoveTemp(PushNotes), false);
	Wave->ConfigureRoomTail(0.016f, 0.32f, 0.30f, 0.16f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateScannerBeep(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGScannerBeep"));
	TArray<FIGToneNote> BeepNotes;
	BeepNotes.Add({0.00f, 0.085f, 2093.0f, 0.200f, 0.03f, 0.6f, EIGToneWaveform::SoftSquare});
	Wave->ConfigureNotes(MoveTemp(BeepNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateDoorlockCode(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGDoorlockCode"));
	TArray<FIGToneNote> Notes;
	// 번호 넷은 사람 손이라 간격이 고르지 않다. 확인 키는 조금 낮다.
	Notes.Add({0.00f, 0.065f, 2349.0f, 0.130f, 0.03f, 0.8f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.31f, 0.065f, 2349.0f, 0.130f, 0.03f, 0.8f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.58f, 0.065f, 2349.0f, 0.130f, 0.03f, 0.8f, EIGToneWaveform::SoftSquare});
	Notes.Add({0.92f, 0.065f, 2349.0f, 0.130f, 0.03f, 0.8f, EIGToneWaveform::SoftSquare});
	Notes.Add({1.30f, 0.085f, 1976.0f, 0.130f, 0.03f, 0.8f, EIGToneWaveform::SoftSquare});
	// 풀리는 음. 도·미·솔·도로 오른다.
	Notes.Add({1.62f, 0.075f, 1047.0f, 0.110f, 0.05f, 1.2f, EIGToneWaveform::Triangle});
	Notes.Add({1.70f, 0.075f, 1319.0f, 0.110f, 0.05f, 1.2f, EIGToneWaveform::Triangle});
	Notes.Add({1.78f, 0.075f, 1568.0f, 0.110f, 0.05f, 1.2f, EIGToneWaveform::Triangle});
	Notes.Add({1.86f, 0.140f, 2093.0f, 0.110f, 0.05f, 1.6f, EIGToneWaveform::Triangle});
	// 모터가 잠금쇠를 끌어당기고 끝에서 철컥.
	Notes.Add({2.02f, 0.32f, 380.0f, 0.070f, 0.15f, 1.0f, EIGToneWaveform::BandNoise, 0.45f});
	Notes.Add({2.34f, 0.03f, 1400.0f, 0.090f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({2.34f, 0.08f, 92.0f, 0.110f, 0.01f, 2.4f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateRegisterSound(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGRegisterSound"));
	TArray<FIGToneNote> RegisterNotes;
	RegisterNotes.Add({0.00f, 0.07f, 1568.0f, 0.150f, 0.03f, 0.6f, EIGToneWaveform::SoftSquare});
	RegisterNotes.Add({0.12f, 0.07f, 1568.0f, 0.150f, 0.03f, 0.6f, EIGToneWaveform::SoftSquare});
	RegisterNotes.Add({0.30f, 0.22f, 680.0f, 0.110f, 0.10f, 1.0f, EIGToneWaveform::ValueNoise});
	RegisterNotes.Add({0.30f, 0.10f, 66.0f, 0.300f, 0.01f, 3.0f, EIGToneWaveform::Sine});
	RegisterNotes.Add({0.52f, 0.05f, 1400.0f, 0.100f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	Wave->ConfigureNotes(MoveTemp(RegisterNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateFluorescentBallastSnap(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGFluorescentBallastSnap"));
	TArray<FIGToneNote> SnapNotes;

	// Contactor/lampholder crack: very short and spectrally broad.
	SnapNotes.Add({
		0.000f, 0.022f, 3900.0f, 0.220f,
		0.015f, 0.75f, EIGToneWaveform::ValueNoise});
	SnapNotes.Add({
		0.006f, 0.034f, 1260.0f, 0.105f,
		0.010f, 1.8f, EIGToneWaveform::SoftSquare});

	// The tube/reflector rings after power drops. The slight detune prevents
	// this from reading as a UI beep.
	SnapNotes.Add({
		0.013f, 0.190f, 119.6f, 0.105f,
		0.010f, 4.2f, EIGToneWaveform::Sine});
	SnapNotes.Add({
		0.013f, 0.145f, 241.3f, 0.042f,
		0.010f, 4.8f, EIGToneWaveform::Sine});
	SnapNotes.Add({
		0.020f, 0.090f, 720.0f, 0.026f,
		0.010f, 3.6f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(SnapNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateMuffledPrayerRadio(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGMuffledPrayerRadio"));
	constexpr float LoopLength = 13.6f;
	TArray<FIGToneNote> RadioNotes;

	// A tiny mains hum and cabinet noise identify a cheap radio. The pulse
	// groups below imitate sentence cadence only; no formants or intelligible
	// speech are generated.
	RadioNotes.Add({
		0.0f, LoopLength, 59.8f, 0.010f,
		0.25f, 0.7f, EIGToneWaveform::Sine});
	RadioNotes.Add({
		0.0f, LoopLength, 92.0f, 0.006f,
		0.25f, 0.7f, EIGToneWaveform::ValueNoise});

	struct FMurmurPulse
	{
		float Start;
		float Duration;
		float BaseHz;
		float Level;
	};
	const FMurmurPulse Pulses[] = {
		{0.45f, 0.82f, 151.0f, 0.034f},
		{1.38f, 0.55f, 172.0f, 0.030f},
		{2.10f, 1.08f, 143.0f, 0.036f},
		{3.72f, 0.68f, 166.0f, 0.029f},
		{4.55f, 1.32f, 148.0f, 0.035f},
		{6.95f, 0.72f, 158.0f, 0.031f},
		{7.82f, 0.46f, 181.0f, 0.027f},
		{8.48f, 1.18f, 146.0f, 0.035f},
		{10.20f, 0.62f, 169.0f, 0.030f},
		{10.98f, 1.50f, 142.0f, 0.034f},
	};
	for (int32 PulseIndex = 0;
		PulseIndex < static_cast<int32>(UE_ARRAY_COUNT(Pulses));
		++PulseIndex)
	{
		const FMurmurPulse& Pulse = Pulses[PulseIndex];
		RadioNotes.Add({
			Pulse.Start,
			Pulse.Duration,
			Pulse.BaseHz,
			Pulse.Level,
			0.18f,
			1.7f,
			EIGToneWaveform::SoftSquare});
		RadioNotes.Add({
			Pulse.Start + 0.025f,
			Pulse.Duration * 0.92f,
			Pulse.BaseHz * (1.43f + (PulseIndex % 3) * 0.035f),
			Pulse.Level * 0.33f,
			0.22f,
			1.9f,
			EIGToneWaveform::ValueNoise});
	}

	Wave->ConfigureNotes(MoveTemp(RadioNotes), true, LoopLength);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCardboardDrag(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGCardboardDrag"));
	TArray<FIGToneNote> DragNotes;

	// Broad but low-energy friction with two changes of pressure.
	DragNotes.Add({
		0.000f, 0.92f, 310.0f, 0.110f,
		0.12f, 1.45f, EIGToneWaveform::ValueNoise});
	DragNotes.Add({
		0.130f, 0.54f, 118.0f, 0.052f,
		0.18f, 1.6f, EIGToneWaveform::Triangle});
	DragNotes.Add({
		0.460f, 0.38f, 520.0f, 0.048f,
		0.08f, 1.8f, EIGToneWaveform::ValueNoise});

	// The box is set down, not thrown.
	DragNotes.Add({
		0.940f, 0.105f, 67.0f, 0.190f,
		0.015f, 3.2f, EIGToneWaveform::Sine});
	DragNotes.Add({
		0.940f, 0.055f, 430.0f, 0.060f,
		0.025f, 2.2f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(DragNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateToolCartRoll(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGToolCartRoll"));
	TArray<FIGToneNote> Notes;
	// 바퀴 넷이 바닥을 구르는 마찰. 카트가 서는 만큼 같이 잦아든다.
	Notes.Add({0.000f, 1.34f, 420.0f, 0.070f, 0.12f, 1.1f, EIGToneWaveform::BandNoise, 0.12f});
	Notes.Add({0.000f, 1.34f, 88.0f, 0.055f, 0.14f, 1.2f, EIGToneWaveform::Sub});
	Notes.Add({0.000f, 1.30f, 1600.0f, 0.020f, 0.10f, 1.3f, EIGToneWaveform::ValueNoise});
	// 금 간 캐스터. 한 바퀴에 한 번 갈라진 턱이 바닥을 친다. 느려질수록 간격이
	// 벌어지고 소리도 작아진다.
	Notes.Add({0.100f, 0.050f, 210.0f, 0.090f, 0.006f, 2.8f, EIGToneWaveform::Sine});
	Notes.Add({0.100f, 0.020f, 1300.0f, 0.050f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.370f, 0.050f, 205.0f, 0.081f, 0.006f, 2.8f, EIGToneWaveform::Sine});
	Notes.Add({0.370f, 0.020f, 1260.0f, 0.045f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.650f, 0.050f, 200.0f, 0.072f, 0.006f, 2.8f, EIGToneWaveform::Sine});
	Notes.Add({0.650f, 0.020f, 1220.0f, 0.040f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.950f, 0.050f, 196.0f, 0.061f, 0.006f, 2.8f, EIGToneWaveform::Sine});
	Notes.Add({0.950f, 0.020f, 1180.0f, 0.034f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.280f, 0.050f, 192.0f, 0.050f, 0.006f, 2.8f, EIGToneWaveform::Sine});
	Notes.Add({1.280f, 0.020f, 1150.0f, 0.028f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
	// 멎는다. 카트 몸이 한 번 흔들리고 선반 위 쇠붙이가 가볍게 부딪힌다.
	Notes.Add({1.340f, 0.180f, 72.0f, 0.100f, 0.015f, 3.0f, EIGToneWaveform::Sine});
	Notes.Add({1.340f, 0.050f, 520.0f, 0.040f, 0.020f, 2.4f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.360f, 0.120f, 3170.0f, 0.012f, 0.010f, 2.8f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateTuningWrenchClink(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGTuningWrenchClink"));
	TArray<FIGToneNote> Notes;
	// 쇠머리가 먼저 닿는다. 짧은 알갱이와 콘크리트의 둔한 몸통.
	Notes.Add({0.000f, 0.022f, 2600.0f, 0.060f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.000f, 0.070f, 190.0f, 0.080f, 0.004f, 2.6f, EIGToneWaveform::Sine});
	// 자루의 굽힘 모드(1 : 2.76 : 5.4). 780Hz가 이 쇠의 목소리다 — 철문
	// 너머에서도 이것만은 남는다.
	Notes.Add({0.003f, 0.460f, 780.0f, 0.052f, 0.004f, 3.2f, EIGToneWaveform::Sine});
	Notes.Add({0.003f, 0.260f, 2150.0f, 0.026f, 0.004f, 3.6f, EIGToneWaveform::Sine});
	Notes.Add({0.003f, 0.140f, 4210.0f, 0.013f, 0.004f, 4.0f, EIGToneWaveform::Sine});
	// 나무 손잡이가 뒤따라 눕는다.
	Notes.Add({0.030f, 0.110f, 240.0f, 0.040f, 0.005f, 2.6f, EIGToneWaveform::Pluck, 0.20f});
	// 한 번 튀었다가 앉는다. 같은 쇠가 작게.
	Notes.Add({0.125f, 0.016f, 2400.0f, 0.028f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.126f, 0.280f, 781.5f, 0.022f, 0.004f, 3.4f, EIGToneWaveform::Sine});
	Notes.Add({0.126f, 0.050f, 190.0f, 0.028f, 0.004f, 2.8f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateJournalPageTurn(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGJournalPageTurn"));
	TArray<FIGToneNote> PaperNotes;

	// A quiet, dry lift followed by two fingertip brushes. The cue deliberately
	// avoids a tonal UI click: the journal is a physical object in the room,
	// even though PlaySound2D marks this one-shot as pause-safe UI audio.
	PaperNotes.Add({
		0.000f, 0.170f, 3400.0f, 0.045f,
		0.08f, 1.8f, EIGToneWaveform::ValueNoise});
	PaperNotes.Add({
		0.025f, 0.110f, 620.0f, 0.028f,
		0.10f, 2.1f, EIGToneWaveform::ValueNoise});
	PaperNotes.Add({
		0.145f, 0.075f, 2100.0f, 0.038f,
		0.05f, 2.4f, EIGToneWaveform::ValueNoise});
	PaperNotes.Add({
		0.205f, 0.050f, 1250.0f, 0.024f,
		0.04f, 2.8f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(PaperNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePaperDoorSlide(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGPaperDoorSlide"));
	TArray<FIGToneNote> PaperNotes;

	// 첫 레이어는 문풍지에 스치는 종이, 두 번째는 문틈을 벗어난 뒤의
	// 넓은 타일 마찰음이다. 마지막 두 번의 짧은 충격음으로 아이템 획득이
	// 아니라 가벼운 종이 한 장이 바닥에 안착했다는 느낌을 준다.
	PaperNotes.Add({
		0.000f, 0.34f, 2850.0f, 0.036f,
		0.10f, 1.55f, EIGToneWaveform::ValueNoise});
	PaperNotes.Add({
		0.110f, 0.58f, 930.0f, 0.052f,
		0.14f, 1.72f, EIGToneWaveform::ValueNoise});
	PaperNotes.Add({
		0.290f, 0.42f, 310.0f, 0.023f,
		0.18f, 1.95f, EIGToneWaveform::ValueNoise});
	PaperNotes.Add({
		0.700f, 0.055f, 1800.0f, 0.026f,
		0.04f, 2.8f, EIGToneWaveform::ValueNoise});
	PaperNotes.Add({
		0.748f, 0.040f, 720.0f, 0.018f,
		0.03f, 3.1f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(PaperNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateStoreJingle(
	UObject* Outer,
	const float PitchSemitones,
	const float TimeScale)
{
	using namespace IGToneSequence;
	UIGToneSequenceSoundWave* Wave = NewWave(Outer, TEXT("IGStoreJingle"));

	const float SafeTimeScale = FMath::Clamp(TimeScale, 0.5f, 2.0f);
	const float PitchRatio = FMath::Pow(2.0f, PitchSemitones / 12.0f);
	constexpr float BaseBeatSeconds = 60.0f / 76.0f;
	const float Beat = BaseBeatSeconds * SafeTimeScale;
	const bool bDegraded =
		!FMath::IsNearlyZero(PitchSemitones)
		|| !FMath::IsNearlyEqual(SafeTimeScale, 1.0f);
	TArray<FIGToneNote> JingleNotes;

	struct FMelodyStep
	{
		float Frequency;
		float StartBeat;
		float Beats;
	};

	// Original cheerful music-box melody: bright, simple and endlessly repeating.
	const FMelodyStep Melody[] = {
		{NoteE4, 0.0f, 1.0f}, {NoteG4, 1.0f, 1.0f}, {NoteA4, 2.0f, 1.0f}, {NoteC5, 3.0f, 1.0f},
		{NoteA4, 4.0f, 1.0f}, {NoteG4, 5.0f, 1.0f}, {NoteE4, 6.0f, 2.0f},
		{NoteG4, 8.0f, 1.0f}, {NoteA4, 9.0f, 1.0f}, {NoteC5, 10.0f, 1.0f}, {NoteD5, 11.0f, 1.0f},
		{NoteC5, 12.0f, 2.0f}, {NoteA4, 14.0f, 2.0f},
		{NoteE5, 16.0f, 1.0f}, {NoteD5, 17.0f, 1.0f}, {NoteC5, 18.0f, 1.0f}, {NoteA4, 19.0f, 1.0f},
		{NoteG4, 20.0f, 1.0f}, {NoteA4, 21.0f, 1.0f}, {NoteC5, 22.0f, 1.0f}, {NoteA4, 23.0f, 1.0f},
		{NoteG4, 24.0f, 1.0f}, {NoteE4, 25.0f, 1.0f}, {NoteD4, 26.0f, 1.0f}, {NoteE4, 27.0f, 1.0f},
		{NoteC4, 28.0f, 3.0f},
	};

	for (int32 StepIndex = 0;
		StepIndex < static_cast<int32>(UE_ARRAY_COUNT(Melody));
		++StepIndex)
	{
		// M1b leaves one expected mallet strike empty; the accompaniment keeps
		// moving, making the absence register before the player names it.
		if (bDegraded && StepIndex == 18)
		{
			continue;
		}
		const FMelodyStep& Step = Melody[StepIndex];
		const float Start = Step.StartBeat * Beat;
		const float NoteLength = Step.Beats * Beat * 0.85f;
		JingleNotes.Add({
			Start, NoteLength, Step.Frequency * PitchRatio,
			0.082f, 0.02f, 2.2f, EIGToneWaveform::Triangle});
		JingleNotes.Add({
			Start, NoteLength * 0.8f, Step.Frequency * 2.0f * PitchRatio,
			0.028f, 0.02f, 2.6f, EIGToneWaveform::Sine});
	}

	// Root bass every two beats: C / Am / F / C, then Am / F / G / C.
	const float BassLine[] = {
		NoteC3, NoteC3, NoteA2, NoteA2, NoteF3, NoteF3, NoteC3, NoteC3,
		NoteA2, NoteA2, NoteF3, NoteF3, NoteG2, NoteG2, NoteC3, NoteC3,
	};
	for (int32 BassIndex = 0; BassIndex < static_cast<int32>(UE_ARRAY_COUNT(BassLine)); ++BassIndex)
	{
		JingleNotes.Add({
			BassIndex * 2.0f * Beat,
			2.0f * Beat * 0.9f,
			BassLine[BassIndex] * PitchRatio,
			0.050f,
			0.04f,
			1.4f,
			EIGToneWaveform::SoftSquare});
	}

	// 같은 멜로디가 대화 위로 곧장 반복되지 않게 안내 방송 사이에 쉼을 둔다.
	Wave->ConfigureNotes(MoveTemp(JingleNotes), true, 32.0f * Beat + (bDegraded ? 0.f : 45.f));
	if (bDegraded)
	{
		Wave->ConfigurePitchWow(0.003f, 0.30f);
	}
	return Wave;
}

UIGToneSequenceSoundWave*
UIGToneSequenceSoundWave::CreateFloodedCorridorWaterBed(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGFloodedCorridorWaterBed"));
	TArray<FIGToneNote> Notes;
	Notes.Add({0.0f, 9.0f, 96.0f, 0.025f, 0.20f, 0.8f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.0f, 9.0f, 52.0f, 0.018f, 0.25f, 0.8f, EIGToneWaveform::Sine});
	Notes.Add({1.2f, 0.24f, 820.0f, 0.065f, 0.02f, 2.8f, EIGToneWaveform::ValueNoise});
	Notes.Add({4.7f, 0.36f, 1280.0f, 0.050f, 0.02f, 3.2f, EIGToneWaveform::Sine});
	Notes.Add({7.4f, 0.20f, 610.0f, 0.052f, 0.02f, 2.6f, EIGToneWaveform::ValueNoise});
	Wave->ConfigureNotes(MoveTemp(Notes), true, 9.0f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateFootstep(
	UObject* Outer,
	const float PitchScale,
	const float Amplitude)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGFootstep"));
	const float SafePitch = FMath::Clamp(PitchScale, 0.5f, 2.0f);
	const float SafeAmplitude = FMath::Clamp(Amplitude, 0.0f, 1.0f);
	TArray<FIGToneNote> StepNotes;
	StepNotes.Add({0.00f, 0.085f, 58.0f * SafePitch, 0.50f * SafeAmplitude, 0.010f, 3.0f, EIGToneWaveform::Sine});
	StepNotes.Add({0.00f, 0.050f, 420.0f * SafePitch, 0.22f * SafeAmplitude, 0.060f, 2.0f, EIGToneWaveform::ValueNoise});
	Wave->ConfigureNotes(MoveTemp(StepNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateAlarmFirstNote(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGAlarmFirstNote"));
	TArray<FIGToneNote> AlarmNotes;

	// 머리맡 폰 알람의 첫 박. 880 Hz 140 ms에 작은 3배음을 얹는다. 밤마다
	// 이 한 박으로 04:30이 시작된다.
	AlarmNotes.Add({0.000f, 0.140f, 880.0f, 0.170f, 0.030f, 1.1f, EIGToneWaveform::Sine});
	AlarmNotes.Add({0.000f, 0.140f, 2640.0f, 0.025f, 0.030f, 1.1f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(AlarmNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCallFailTone(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGCallFailTone"));
	TArray<FIGToneNote> FailNotes;

	// Flat handset beeps stepping down: the network refusing, not a jump cue.
	FailNotes.Add({0.000f, 0.180f, 425.0f, 0.085f, 0.040f, 1.4f, EIGToneWaveform::SoftSquare});
	FailNotes.Add({0.260f, 0.240f, 355.0f, 0.075f, 0.040f, 1.8f, EIGToneWaveform::SoftSquare});

	Wave->ConfigureNotes(MoveTemp(FailNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCallRingback(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGCallRingback"));
	TArray<FIGToneNote> RingNotes;

	// 국내 연결음은 440Hz와 480Hz를 겹친 음이라 초당 40번 맥놀이를 한다.
	// 1초 울리고 2초 쉰다. 425Hz 단음은 이 게임에서 통화 실패음 몫이다.
	RingNotes.Add({0.000f, 1.000f, 440.0f, 0.062f, 0.010f, 0.25f, EIGToneWaveform::Sine});
	RingNotes.Add({0.000f, 1.000f, 480.0f, 0.062f, 0.010f, 0.25f, EIGToneWaveform::Sine});
	// 두 번째 신호는 울리다가 받는 순간 끊긴다.
	RingNotes.Add({3.000f, 0.340f, 440.0f, 0.062f, 0.030f, 0.25f, EIGToneWaveform::Sine});
	RingNotes.Add({3.000f, 0.340f, 480.0f, 0.062f, 0.030f, 0.25f, EIGToneWaveform::Sine});
	// 받는 딸깍.
	RingNotes.Add({3.340f, 0.022f, 2400.0f, 0.080f, 0.005f, 4.0f, EIGToneWaveform::ValueNoise});
	RingNotes.Add({3.340f, 0.045f, 300.0f, 0.040f, 0.005f, 3.6f, EIGToneWaveform::Sine});
	// 열린 회선의 숨. 누가 받았다는 것까지만 들린다.
	RingNotes.Add({3.370f, 0.700f, 3200.0f, 0.010f, 0.250f, 1.2f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(RingNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateServiceBellDing(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGServiceBellDing"));
	TArray<FIGToneNote> BellNotes;
	// 손바닥에 눌린 막대가 종을 친다. 아주 짧고 밝다.
	BellNotes.Add({0.000f, 0.014f, 3200.0f, 0.028f, 0.005f, 4.0f, EIGToneWaveform::ValueNoise});
	// 종의 몸. 거의 겹친 두 모드가 느리게 맥놀이한다.
	BellNotes.Add({0.002f, 2.200f, 2350.0f, 0.050f, 0.003f, 3.0f, EIGToneWaveform::Sine});
	BellNotes.Add({0.002f, 1.900f, 2356.0f, 0.016f, 0.003f, 3.0f, EIGToneWaveform::Sine});
	// 배음에서 벗어난 윗모드. 금방 죽는다.
	BellNotes.Add({0.002f, 0.550f, 5880.0f, 0.018f, 0.004f, 3.2f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(BellNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateRelayClick(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGRelayClick"));
	TArray<FIGToneNote> ClickNotes;

	ClickNotes.Add({0.000f, 0.022f, 2400.0f, 0.100f, 0.005f, 4.0f, EIGToneWaveform::ValueNoise});
	ClickNotes.Add({0.000f, 0.045f, 300.0f, 0.055f, 0.005f, 3.6f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(ClickNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateBreakerThrow(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGBreakerThrow"));
	TArray<FIGToneNote> ThrowNotes;

	// 손잡이가 넘어가며 접점이 붙고, 레버가 멈추는 몸통 뒤로 철제 함이 짧게 운다.
	ThrowNotes.Add({0.000f, 0.014f, 3200.0f, 0.180f, 0.03f, 2.0f, EIGToneWaveform::WhiteNoise});
	ThrowNotes.Add({0.000f, 0.024f, 2400.0f, 0.110f, 0.02f, 3.0f, EIGToneWaveform::ValueNoise});
	ThrowNotes.Add({0.008f, 0.075f, 110.0f, 0.170f, 0.03f, 2.4f, EIGToneWaveform::Sub});
	ThrowNotes.Add({0.010f, 0.260f, 1250.0f, 0.060f, 0.02f, 2.2f, EIGToneWaveform::BandNoise, 0.70f});

	Wave->ConfigureNotes(MoveTemp(ThrowNotes), false);
	// 로비는 딱딱한 방이다. 함 소리가 벽 사이에서 한 번 되울린다.
	Wave->ConfigureRoomTail(0.012f, 0.28f, 0.35f, 0.12f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePlasticBagSetDown(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGPlasticBagSetDown"));
	TArray<FIGToneNote> BagNotes;

	// Film crinkle spreading as the load transfers, then bottles knock once.
	BagNotes.Add({0.000f, 0.240f, 3800.0f, 0.050f, 0.100f, 2.0f, EIGToneWaveform::ValueNoise});
	BagNotes.Add({0.060f, 0.180f, 2500.0f, 0.040f, 0.120f, 2.2f, EIGToneWaveform::ValueNoise});
	BagNotes.Add({0.190f, 0.070f, 210.0f, 0.060f, 0.010f, 3.0f, EIGToneWaveform::Sine});
	BagNotes.Add({0.240f, 0.055f, 260.0f, 0.038f, 0.010f, 3.2f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(BagNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePlasticBagLift(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGPlasticBagLift"));
	TArray<FIGToneNote> BagNotes;

	// Handles stretching taut first, then the shorter gather crinkle.
	BagNotes.Add({0.000f, 0.140f, 1400.0f, 0.030f, 0.300f, 1.8f, EIGToneWaveform::ValueNoise});
	BagNotes.Add({0.090f, 0.190f, 3300.0f, 0.045f, 0.120f, 2.2f, EIGToneWaveform::ValueNoise});
	BagNotes.Add({0.210f, 0.045f, 290.0f, 0.030f, 0.010f, 3.2f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(BagNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateClothSettle(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGClothSettle"));
	TArray<FIGToneNote> ClothNotes;

	// A single sinking weight: broadband cloth friction that decays without
	// any rhythm. Deliberately nothing like breathing.
	ClothNotes.Add({0.000f, 0.460f, 1200.0f, 0.038f, 0.180f, 2.6f, EIGToneWaveform::ValueNoise});
	ClothNotes.Add({0.050f, 0.360f, 500.0f, 0.030f, 0.220f, 2.8f, EIGToneWaveform::ValueNoise});
	ClothNotes.Add({0.260f, 0.180f, 90.0f, 0.026f, 0.150f, 3.0f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(ClothNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateVentDuctSpinUp(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGVentDuctSpinUp"));
	TArray<FIGToneNote> DuctNotes;

	// Flexible duct sections unfolding in three pulls...
	DuctNotes.Add({0.000f, 0.260f, 900.0f, 0.045f, 0.080f, 2.2f, EIGToneWaveform::ValueNoise});
	DuctNotes.Add({0.320f, 0.220f, 1150.0f, 0.040f, 0.080f, 2.2f, EIGToneWaveform::ValueNoise});
	DuctNotes.Add({0.600f, 0.240f, 800.0f, 0.038f, 0.080f, 2.4f, EIGToneWaveform::ValueNoise});
	// ...then the fan comes up and holds a steady low idle.
	DuctNotes.Add({0.900f, 1.700f, 68.0f, 0.055f, 0.350f, 0.9f, EIGToneWaveform::SoftSquare});
	DuctNotes.Add({0.900f, 1.700f, 136.0f, 0.022f, 0.350f, 0.9f, EIGToneWaveform::SoftSquare});
	DuctNotes.Add({1.100f, 1.500f, 400.0f, 0.014f, 0.300f, 1.0f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(DuctNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePhoneVibrationUnfinished(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGPhoneVibrationUnfinished"));
	TArray<FIGToneNote> BuzzNotes;

	// Motor buzz on a wooden desk: two full bars, then the third cut short.
	// Nothing answers it and the screen never lights.
	const float BarStarts[] = {0.000f, 0.560f, 1.120f};
	const float BarLengths[] = {0.360f, 0.360f, 0.120f};
	for (int32 BarIndex = 0; BarIndex < 3; ++BarIndex)
	{
		BuzzNotes.Add({
			BarStarts[BarIndex], BarLengths[BarIndex], 178.0f, 0.060f,
			0.040f, BarIndex == 2 ? 8.0f : 1.2f, EIGToneWaveform::SoftSquare});
		BuzzNotes.Add({
			BarStarts[BarIndex], BarLengths[BarIndex], 356.0f, 0.024f,
			0.040f, BarIndex == 2 ? 8.0f : 1.2f, EIGToneWaveform::SoftSquare});
		BuzzNotes.Add({
			BarStarts[BarIndex], BarLengths[BarIndex], 89.0f, 0.030f,
			0.040f, BarIndex == 2 ? 8.0f : 1.2f, EIGToneWaveform::Sine});
	}

	Wave->ConfigureNotes(MoveTemp(BuzzNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateTrappedBreathBed(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGTrappedBreathBed"));
	constexpr float LoopLength = 5.2f;
	TArray<FIGToneNote> Notes;

	// Two asymmetric breaths. Value-noise is kept very low and banded by a
	// sine body so it reads as air against cloth, not a white-noise generator.
	Notes.Add({
		0.35f, 1.05f, 182.0f, 0.055f,
		0.36f, 1.25f, EIGToneWaveform::ValueNoise});
	Notes.Add({
		0.42f, 0.92f, 61.0f, 0.021f,
		0.42f, 1.10f, EIGToneWaveform::Sine});
	Notes.Add({
		2.45f, 1.38f, 154.0f, 0.048f,
		0.28f, 1.75f, EIGToneWaveform::ValueNoise});
	Notes.Add({
		2.55f, 1.14f, 54.0f, 0.018f,
		0.34f, 1.55f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopLength);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateSurfaceFootstep(
	UObject* Outer,
	const EIGFootstepSurface Surface,
	const float VariationPitch,
	const float Amplitude)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		TEXT("IGSurfaceFootstep"));
	const float Pitch = FMath::Clamp(VariationPitch, 0.88f, 1.12f);
	const float Gain = FMath::Clamp(Amplitude, 0.0f, 1.0f);
	TArray<FIGToneNote> Notes;

	switch (Surface)
	{
	// 걸음 하나는 셋이다: 뒤꿈치가 닿는 밀리초의 트랜지언트(백색 잡음), 밑창이
	// 눌리는 몸통(대역 잡음), 표면이 내는 꼬리. 예전엔 사인 하나에 값잡음 하나라
	// 「붕」 소리에 가까웠다.
	case EIGFootstepSurface::Vinyl:
		Notes.Add({0.000f, 0.006f, 3000.0f, 0.26f * Gain, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
		Notes.Add({0.000f, 0.080f, 62.0f * Pitch, 0.30f * Gain, 0.010f, 3.2f, EIGToneWaveform::Sub});
		Notes.Add({0.002f, 0.070f, 900.0f * Pitch, 0.22f * Gain, 0.05f, 2.4f, EIGToneWaveform::BandNoise, 0.35f});
		Notes.Add({0.012f, 0.060f, 2600.0f * Pitch, 0.09f * Gain, 0.10f, 2.0f, EIGToneWaveform::BandNoise, 0.50f});
		break;

	case EIGFootstepSurface::Concrete:
		Notes.Add({0.000f, 0.005f, 4000.0f, 0.28f * Gain, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
		Notes.Add({0.000f, 0.095f, 57.0f * Pitch, 0.34f * Gain, 0.010f, 3.0f, EIGToneWaveform::Sub});
		Notes.Add({0.001f, 0.110f, 1400.0f * Pitch, 0.22f * Gain, 0.04f, 2.2f, EIGToneWaveform::BandNoise, 0.25f});
		Notes.Add({0.030f, 0.180f, 420.0f * Pitch, 0.10f * Gain, 0.08f, 2.0f, EIGToneWaveform::BandNoise, 0.45f});
		Notes.Add({0.020f, 0.120f, 900.0f, 0.08f * Gain, 0.10f, 1.8f, EIGToneWaveform::Crackle});
		break;

	case EIGFootstepSurface::MetalStair:
		Notes.Add({0.000f, 0.005f, 4500.0f, 0.28f * Gain, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
		Notes.Add({0.000f, 0.070f, 72.0f * Pitch, 0.30f * Gain, 0.010f, 3.0f, EIGToneWaveform::Sub});
		Notes.Add({0.000f, 0.050f, 2500.0f * Pitch, 0.16f * Gain, 0.02f, 2.0f, EIGToneWaveform::BandNoise, 0.30f});
		// 디딤판이 운다. 튕긴 줄이 금속판에 가장 가깝다.
		Notes.Add({0.006f, 0.420f, 1480.0f * Pitch, 0.14f * Gain, 0.010f, 2.2f, EIGToneWaveform::Pluck, 0.62f});
		Notes.Add({0.006f, 0.360f, 2780.0f * Pitch, 0.06f * Gain, 0.010f, 2.6f, EIGToneWaveform::Pluck, 0.50f});
		Notes.Add({0.010f, 0.250f, 210.0f * Pitch, 0.08f * Gain, 0.05f, 2.4f, EIGToneWaveform::BandNoise, 0.55f});
		break;

	case EIGFootstepSurface::Rooftop:
		Notes.Add({0.000f, 0.005f, 2500.0f, 0.14f * Gain, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
		Notes.Add({0.000f, 0.085f, 52.0f * Pitch, 0.38f * Gain, 0.010f, 3.4f, EIGToneWaveform::Sub});
		Notes.Add({0.002f, 0.090f, 380.0f * Pitch, 0.18f * Gain, 0.06f, 2.2f, EIGToneWaveform::BandNoise, 0.30f});
		break;

	case EIGFootstepSurface::GypsumDebris:
		Notes.Add({0.000f, 0.005f, 4000.0f, 0.22f * Gain, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
		Notes.Add({0.000f, 0.075f, 64.0f * Pitch, 0.28f * Gain, 0.010f, 3.0f, EIGToneWaveform::Sub});
		// 부스러기는 알갱이다. 밀도 700/s의 알갱이가 0.16초 동안 갈린다.
		Notes.Add({0.000f, 0.160f, 700.0f, 0.26f * Gain, 0.02f, 1.6f, EIGToneWaveform::Crackle});
		Notes.Add({0.010f, 0.090f, 1900.0f * Pitch, 0.14f * Gain, 0.05f, 2.0f, EIGToneWaveform::BandNoise, 0.30f});
		Notes.Add({0.050f, 0.140f, 1200.0f, 0.10f * Gain, 0.10f, 2.2f, EIGToneWaveform::Crackle});
		break;

	case EIGFootstepSurface::Water:
		Notes.Add({0.000f, 0.100f, 49.0f * Pitch, 0.26f * Gain, 0.010f, 2.8f, EIGToneWaveform::Sub});
		Notes.Add({0.000f, 0.160f, 1200.0f * Pitch, 0.24f * Gain, 0.03f, 1.8f, EIGToneWaveform::BandNoise, 0.20f});
		Notes.Add({0.030f, 0.200f, 600.0f * Pitch, 0.14f * Gain, 0.10f, 1.8f, EIGToneWaveform::BandNoise, 0.35f});
		Notes.Add({0.060f, 0.220f, 260.0f, 0.12f * Gain, 0.10f, 1.8f, EIGToneWaveform::Crackle});
		Notes.Add({0.020f, 0.120f, 3200.0f / Pitch, 0.08f * Gain, 0.05f, 2.4f, EIGToneWaveform::BandNoise, 0.50f});
		break;
	}

	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateTuningMotif(
	UObject* Outer,
	const bool bResolvedEndingA)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		TEXT("IGMissingFloorTuningMotif"));
	constexpr int32 StrikeCount = 8;
	constexpr float StrikeSpacing = 1.70f;
	constexpr float LoopLength = StrikeCount * StrikeSpacing;
	TArray<FIGToneNote> Notes;
	for (int32 Strike = 0; Strike < StrikeCount; ++Strike)
	{
		const float Cents = -30.0f + Strike * 2.0f;
		const float Frequency = 220.0f * FMath::Pow(2.0f, Cents / 1200.0f);
		const float Start = Strike * StrikeSpacing;
		Notes.Add({Start, 1.18f, Frequency, 0.075f, 0.025f, 2.0f, EIGToneWaveform::Triangle});
		Notes.Add({Start, 0.34f, Frequency * 2.0f, 0.016f, 0.030f, 2.8f, EIGToneWaveform::Triangle});
	}
	if (bResolvedEndingA)
	{
		// The only consonant answer in the score bible: a quiet open fifth
		// arrives after the eighth strike instead of resetting unresolved.
		Notes.Add({LoopLength - 1.10f, 1.05f, 329.63f, 0.038f, 0.10f, 2.0f, EIGToneWaveform::Triangle});
		Notes.Add({LoopLength - 1.10f, 1.05f, 440.00f, 0.030f, 0.10f, 2.0f, EIGToneWaveform::Triangle});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), !bResolvedEndingA, LoopLength);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateTuningStrike(
	UObject* Outer,
	const int32 ConfirmationIndex)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		TEXT("IGMissingFloorTruthTuningStrike"));
	// Ten truths stop twelve cents short of concert A. Only Ending A may add
	// the consonant resolution authored by CreateTuningMotif(true).
	const int32 Strike = FMath::Clamp(ConfirmationIndex - 1, 0, 9);
	const float Cents = -30.0f + Strike * 2.0f;
	const float Frequency = 220.0f * FMath::Pow(2.0f, Cents / 1200.0f);
	TArray<FIGToneNote> Notes;
	Notes.Add({0.0f, 1.18f, Frequency, 0.075f, 0.025f, 2.0f,
		EIGToneWaveform::Triangle});
	Notes.Add({0.0f, 0.34f, Frequency * 2.0f, 0.016f, 0.030f, 2.8f,
		EIGToneWaveform::Triangle});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCavityDrone(
	UObject* Outer,
	const int32 NightIndex,
	const bool bFinale)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		TEXT("IGMissingFloorCavityDrone"));
	constexpr float LoopLength = 8.0f;
	TArray<FIGToneNote> Notes;
	Notes.Add({0.0f, LoopLength, 44.0f, 0.095f, 0.35f, 0.8f, EIGToneWaveform::Sine});
	Notes.Add({0.0f, LoopLength, 180.0f, 0.027f, 0.30f, 0.9f, EIGToneWaveform::ValueNoise});
	// 벽 사이 공간의 목울림과 좁은 대역의 숨. 사인과 값잡음만으로는 이명이었다.
	Notes.Add({0.0f, LoopLength, 33.0f, 0.050f, 0.40f, 0.8f, EIGToneWaveform::Growl, 0.25f});
	Notes.Add({0.6f, 3.4f, 210.0f, 0.030f, 0.60f, 1.0f, EIGToneWaveform::BandNoise, 0.85f});
	Notes.Add({4.4f, 3.2f, 236.0f, 0.026f, 0.60f, 1.0f, EIGToneWaveform::BandNoise, 0.85f});
	for (const float Tick : {1.30f, 3.85f, 6.70f})
	{
		Notes.Add({Tick, 0.035f, 980.0f + Tick * 41.0f, 0.055f, 0.010f, 3.2f, EIGToneWaveform::Sine});
		Notes.Add({Tick, 0.090f, 420.0f + Tick * 13.0f, 0.026f, 0.020f, 2.8f, EIGToneWaveform::ValueNoise});
	}
	// 밤3부터 벽 안의 숨이 두 배로 잦아지고 물 틱이 사이사이 끼어든다. 박은 여전히
	// 없다. 그가 서두르는 것이지 음악이 달리는 것은 아니다.
	if (NightIndex >= 3)
	{
		Notes.Add({2.2f, 2.4f, 262.0f, 0.022f, 0.55f, 1.0f, EIGToneWaveform::BandNoise, 0.85f});
		Notes.Add({6.0f, 1.9f, 224.0f, 0.020f, 0.55f, 1.0f, EIGToneWaveform::BandNoise, 0.85f});
		Notes.Add({2.45f, 0.035f, 1080.0f, 0.045f, 0.010f, 3.2f, EIGToneWaveform::Sine});
		Notes.Add({2.45f, 0.090f, 452.0f, 0.022f, 0.020f, 2.8f, EIGToneWaveform::ValueNoise});
		Notes.Add({5.35f, 0.035f, 1199.0f, 0.045f, 0.010f, 3.2f, EIGToneWaveform::Sine});
		Notes.Add({5.35f, 0.090f, 490.0f, 0.022f, 0.020f, 2.8f, EIGToneWaveform::ValueNoise});
	}
	// 밤4: 목울림 위에 한 옥타브 높은 거친 울림, 그리고 루프마다 부풀었다 가라앉는
	// 가는 긴장음 한 줄.
	if (NightIndex >= 4)
	{
		Notes.Add({0.0f, LoopLength, 66.0f, 0.028f, 0.45f, 0.8f, EIGToneWaveform::Growl, 0.45f});
		Notes.Add({0.0f, LoopLength, 1480.0f, 0.016f, 0.55f, 1.4f, EIGToneWaveform::BandNoise, 0.92f});
	}
	// 대치의 드론. 열 번째 진실의 음을 루프마다 한 번 친다. 게임 내내 닿지 못한
	// 음이고, 이 방에서도 닿지 않는다.
	if (bFinale)
	{
		Notes.Add({2.2f, 1.18f, 218.5f, 0.055f, 0.025f, 2.0f, EIGToneWaveform::Triangle});
		Notes.Add({2.2f, 0.34f, 437.0f, 0.012f, 0.030f, 2.8f, EIGToneWaveform::Triangle});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopLength);
	Wave->ConfigurePitchWow(0.0035f, 0.07f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateChaseScore(
	UObject* Outer,
	const int32 NightIndex)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		TEXT("IGMissingFloorChaseScore"));
	constexpr float Beat = 60.0f / 118.0f;
	constexpr int32 BeatCount = 8;
	constexpr float LoopLength = Beat * BeatCount;
	TArray<FIGToneNote> Notes;
	for (int32 Index = 0; Index < BeatCount; ++Index)
	{
		const float Start = Index * Beat;
		const float Accent = Index % 4 == 0 ? 1.0f : 0.72f;
		Notes.Add({Start, Beat * 0.54f, 52.0f, 0.15f * Accent, 0.025f, 2.4f, EIGToneWaveform::Sub});
		Notes.Add({Start, 0.040f, 520.0f, 0.055f * Accent, 0.010f, 3.2f, EIGToneWaveform::ValueNoise});
		// 뒷박의 쇳소리와 앞박의 타격 접촉. 사인 펄스만으로는 심박이지 추격이 아니었다.
		Notes.Add({Start + Beat * 0.5f, 0.050f, 5200.0f, 0.045f, 0.05f, 2.0f, EIGToneWaveform::BandNoise, 0.60f});
		Notes.Add({Start, 0.006f, 3000.0f, 0.060f * Accent, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
		// 밤3부터는 앞박에도 쇳소리가 붙어 8분음표로 몰아친다.
		if (NightIndex >= 3)
		{
			Notes.Add({Start, 0.050f, 5200.0f, 0.032f * Accent, 0.05f, 2.0f, EIGToneWaveform::BandNoise, 0.60f});
		}
		// 밤4는 둘째 마디의 펄스를 반 박으로 쪼갠다. 마디 끝으로 갈수록 발이 빨라진다.
		if (NightIndex >= 4 && Index >= 4)
		{
			Notes.Add({Start + Beat * 0.5f, Beat * 0.30f, 52.0f, 0.09f, 0.02f, 2.4f, EIGToneWaveform::Sub});
		}
	}
	for (const float Frequency : {220.0f, 223.0f, 227.0f})
	{
		Notes.Add({Beat * 0.50f, LoopLength - Beat * 0.50f, Frequency, 0.035f, 0.12f, 1.1f, EIGToneWaveform::Triangle});
	}
	// 밤4의 넷째 불협 줄. 세 줄로 버티던 클러스터가 더 비좁아진다.
	if (NightIndex >= 4)
	{
		Notes.Add({Beat * 0.50f, LoopLength - Beat * 0.50f, 231.0f, 0.028f, 0.12f, 1.1f, EIGToneWaveform::Triangle});
	}
	// 목울림 드론과 넷째·여덟째 박의 튕긴 불협. 조율 안 된 피아노를 주먹으로 친다.
	// 밤3부터는 목울림이 더 거칠다.
	Notes.Add({0.0f, LoopLength, 41.0f, 0.060f, 0.30f, 0.8f, EIGToneWaveform::Growl, NightIndex >= 3 ? 0.50f : 0.35f});
	// 밤1은 뛰면 벗어나는 밤이다. 주먹으로 치는 불협 없이 펄스와 클러스터만 간다.
	if (NightIndex >= 2)
	{
		Notes.Add({Beat * 3.0f, 0.60f, 220.0f, 0.070f, 0.01f, 1.5f, EIGToneWaveform::Pluck, 0.85f});
		Notes.Add({Beat * 3.0f, 0.60f, 233.0f, 0.060f, 0.01f, 1.5f, EIGToneWaveform::Pluck, 0.85f});
		// 마지막 타격이 마디를 넘으면 ConfigureNotes가 루프를 늘려 매번 박자가 쉰다.
		Notes.Add({Beat * 7.0f, Beat * 0.95f, 227.0f, 0.070f, 0.01f, 1.5f, EIGToneWaveform::Pluck, 0.85f});
		Notes.Add({Beat * 7.0f, Beat * 0.95f, 247.0f, 0.060f, 0.01f, 1.5f, EIGToneWaveform::Pluck, 0.85f});
	}
	// 밤3부터 둘째·여섯째 박에 한 옥타브 위의 불협이 짧게 선다.
	if (NightIndex >= 3)
	{
		Notes.Add({Beat * 1.0f, 0.30f, 440.0f, 0.018f, 0.01f, 1.4f, EIGToneWaveform::Triangle});
		Notes.Add({Beat * 1.0f, 0.30f, 446.0f, 0.018f, 0.01f, 1.4f, EIGToneWaveform::Triangle});
		Notes.Add({Beat * 1.0f, 0.30f, 454.0f, 0.018f, 0.01f, 1.4f, EIGToneWaveform::Triangle});
		Notes.Add({Beat * 5.0f, 0.30f, 440.0f, 0.018f, 0.01f, 1.4f, EIGToneWaveform::Triangle});
		Notes.Add({Beat * 5.0f, 0.30f, 446.0f, 0.018f, 0.01f, 1.4f, EIGToneWaveform::Triangle});
		Notes.Add({Beat * 5.0f, 0.30f, 454.0f, 0.018f, 0.01f, 1.4f, EIGToneWaveform::Triangle});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopLength);
	Wave->ConfigurePitchWow(0.0025f, 0.21f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateChaseTail(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		TEXT("IGMissingFloorChaseTail"));
	TArray<FIGToneNote> Notes;
	// 추격 루프의 클러스터를 한 번 튕기고 그대로 울린다. 펄스가 빠진 자리에 남는
	// 것은 조율 안 된 세 줄의 여운뿐이다.
	Notes.Add({0.0f, 3.6f, 220.0f, 0.060f, 0.01f, 1.5f, EIGToneWaveform::Pluck, 0.95f});
	Notes.Add({0.0f, 3.6f, 223.0f, 0.060f, 0.01f, 1.5f, EIGToneWaveform::Pluck, 0.95f});
	Notes.Add({0.0f, 3.6f, 227.0f, 0.060f, 0.01f, 1.5f, EIGToneWaveform::Pluck, 0.95f});
	Notes.Add({0.0f, 4.0f, 220.0f, 0.030f, 0.02f, 2.2f, EIGToneWaveform::Triangle});
	Notes.Add({0.0f, 4.0f, 223.0f, 0.030f, 0.02f, 2.2f, EIGToneWaveform::Triangle});
	Notes.Add({0.0f, 4.0f, 227.0f, 0.030f, 0.02f, 2.2f, EIGToneWaveform::Triangle});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	Wave->ConfigurePitchWow(0.0025f, 0.21f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateWallKnockTriple(
	UObject* Outer,
	const float Muffle01)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGWallKnockTriple"));
	TArray<FIGToneNote> KnockNotes;

	// Each knock is a low structural thump plus a knuckle contact click.
	// Muffling (heard through a finished wall) keeps the thump and eats the
	// click, which is what real drywall does to a fist.
	const float Click = FMath::Lerp(0.085f, 0.012f, FMath::Clamp(Muffle01, 0.0f, 1.0f));
	const float Body = FMath::Lerp(0.360f, 0.300f, FMath::Clamp(Muffle01, 0.0f, 1.0f));
	// 주먹이 석고보드를 치면: 관절 접촉의 밀리초, 판이 흔들리는 저역, 스터드가
	// 울리는 나무 공명. 먹먹할수록 접촉과 공명이 먼저 죽는다.
	const float Stud = FMath::Lerp(0.150f, 0.050f, FMath::Clamp(Muffle01, 0.0f, 1.0f));
	for (int32 KnockIndex = 0; KnockIndex < 3; ++KnockIndex)
	{
		const float Start = 0.62f * KnockIndex;
		KnockNotes.Add({Start, 0.110f, 58.0f, Body, 0.004f, 2.6f, EIGToneWaveform::Sub});
		KnockNotes.Add({Start, 0.060f, 176.0f, 0.090f, 0.006f, 2.0f, EIGToneWaveform::Sine});
		KnockNotes.Add({Start, 0.030f, 1150.0f, Click, 0.020f, 1.2f, EIGToneWaveform::ValueNoise});
		KnockNotes.Add({Start, 0.004f, 3800.0f, Click, 0.050f, 1.0f, EIGToneWaveform::WhiteNoise});
		KnockNotes.Add({Start, 0.220f, 196.0f, Stud, 0.004f, 2.2f, EIGToneWaveform::Pluck, 0.42f});
	}

	Wave->ConfigureNotes(MoveTemp(KnockNotes), false);
	// 복도 벽 사이의 되울림. 21ms 지연이면 7m 남짓 건너편 벽이다.
	Wave->ConfigureRoomTail(0.021f, 0.40f, 0.35f, 0.24f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateWallKnockSingle(
	UObject* Outer,
	const float Muffle01)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGWallKnockSingle"));
	TArray<FIGToneNote> KnockNotes;
	const float Muffle = FMath::Clamp(Muffle01, 0.0f, 1.0f);
	const float Click = FMath::Lerp(0.085f, 0.012f, Muffle);
	const float Body = FMath::Lerp(0.360f, 0.300f, Muffle);
	const float Stud = FMath::Lerp(0.150f, 0.050f, Muffle);
	KnockNotes.Add({0.0f, 0.110f, 58.0f, Body, 0.004f, 2.6f, EIGToneWaveform::Sub});
	KnockNotes.Add({0.0f, 0.060f, 176.0f, 0.090f, 0.006f, 2.0f, EIGToneWaveform::Sine});
	KnockNotes.Add({0.0f, 0.030f, 1150.0f, Click, 0.020f, 1.2f, EIGToneWaveform::ValueNoise});
	KnockNotes.Add({0.0f, 0.004f, 3800.0f, Click, 0.050f, 1.0f, EIGToneWaveform::WhiteNoise});
	KnockNotes.Add({0.0f, 0.220f, 196.0f, Stud, 0.004f, 2.2f, EIGToneWaveform::Pluck, 0.42f});
	Wave->ConfigureNotes(MoveTemp(KnockNotes), false);
	Wave->ConfigureRoomTail(0.021f, 0.40f, 0.35f, 0.24f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateWallKnockReply(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGWallKnockReply"));
	TArray<FIGToneNote> ReplyNotes;

	// Two knocks only, softer and closer together than the hunting triple:
	// an answer, not a search.
	for (int32 KnockIndex = 0; KnockIndex < 2; ++KnockIndex)
	{
		const float Start = 0.42f * KnockIndex;
		ReplyNotes.Add({Start, 0.100f, 58.0f, 0.250f, 0.005f, 2.8f, EIGToneWaveform::Sub});
		ReplyNotes.Add({Start, 0.050f, 176.0f, 0.060f, 0.008f, 2.0f, EIGToneWaveform::Sine});
		ReplyNotes.Add({Start, 0.024f, 1000.0f, 0.030f, 0.030f, 1.4f, EIGToneWaveform::ValueNoise});
		ReplyNotes.Add({Start, 0.003f, 3600.0f, 0.040f, 0.050f, 1.0f, EIGToneWaveform::WhiteNoise});
		ReplyNotes.Add({Start, 0.200f, 196.0f, 0.090f, 0.004f, 2.2f, EIGToneWaveform::Pluck, 0.38f});
	}

	Wave->ConfigureNotes(MoveTemp(ReplyNotes), false);
	// 포획 노크는 드라이로 재생되지만(§21.3) 파형 자체의 스터드 울림은 남는다 —
	// 방이 없다는 뜻이지 벽이 없다는 뜻은 아니다. 방 울림은 걸지 않는다.
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateAnswerKnockPattern(
	UObject* Outer,
	const float Muffle01)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGAnswerKnockPattern"));
	TArray<FIGToneNote> PatternNotes;

	// Two, a rest, one. The rest is the signature: 0.42 s inside the pair,
	// 0.73 s of silence, then the single settling knock.
	const float Click = FMath::Lerp(0.075f, 0.010f, FMath::Clamp(Muffle01, 0.0f, 1.0f));
	const float Body = FMath::Lerp(0.330f, 0.270f, FMath::Clamp(Muffle01, 0.0f, 1.0f));
	const float Stud = FMath::Lerp(0.140f, 0.045f, FMath::Clamp(Muffle01, 0.0f, 1.0f));
	const float Starts[] = {0.0f, 0.42f, 1.15f};
	for (const float Start : Starts)
	{
		PatternNotes.Add({Start, 0.105f, 58.0f, Body, 0.004f, 2.6f, EIGToneWaveform::Sub});
		PatternNotes.Add({Start, 0.055f, 176.0f, 0.080f, 0.006f, 2.0f, EIGToneWaveform::Sine});
		PatternNotes.Add({Start, 0.028f, 1100.0f, Click, 0.020f, 1.2f, EIGToneWaveform::ValueNoise});
		PatternNotes.Add({Start, 0.004f, 3600.0f, Click, 0.050f, 1.0f, EIGToneWaveform::WhiteNoise});
		PatternNotes.Add({Start, 0.210f, 196.0f, Stud, 0.004f, 2.2f, EIGToneWaveform::Pluck, 0.40f});
	}

	Wave->ConfigureNotes(MoveTemp(PatternNotes), false);
	Wave->ConfigureRoomTail(0.018f, 0.36f, 0.30f, 0.20f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateEntityDragLoop(
	UObject* Outer,
	const bool bVinyl)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		bVinyl ? TEXT("IGEntityDragLoopVinyl") : TEXT("IGEntityDragLoop"));
	TArray<FIGToneNote> DragNotes;

	if (bVinyl)
	{
		// 장판: sheet vinyl laid on screed. The slab is still there but a
		// millimetre of plastic sits over it, so the rumble goes and the drag
		// becomes a higher, tighter hiss. The palm plant lands softer.
		DragNotes.Add({0.000f, 0.060f, 78.0f, 0.130f, 0.010f, 2.8f, EIGToneWaveform::Sine});
		DragNotes.Add({0.000f, 0.045f, 1100.0f, 0.055f, 0.026f, 1.6f, EIGToneWaveform::ValueNoise});
		DragNotes.Add({0.180f, 0.520f, 900.0f, 0.078f, 0.180f, 1.2f, EIGToneWaveform::ValueNoise});
		DragNotes.Add({0.180f, 0.520f, 2200.0f, 0.038f, 0.200f, 1.3f, EIGToneWaveform::ValueNoise});
		// Stick-slip. Cloth on plastic catches and releases, and that squeak is
		// the single clearest sign underfoot that he has come in off the tile.
		DragNotes.Add({0.315f, 0.070f, 1750.0f, 0.030f, 0.060f, 2.2f, EIGToneWaveform::Triangle});
		DragNotes.Add({0.612f, 0.055f, 2050.0f, 0.022f, 0.070f, 2.4f, EIGToneWaveform::Triangle});
		DragNotes.Add({0.740f, 0.180f, 2600.0f, 0.020f, 0.200f, 1.8f, EIGToneWaveform::ValueNoise});
		DragNotes.Add({0.180f, 0.520f, 1600.0f, 0.050f, 0.180f, 1.2f, EIGToneWaveform::BandNoise, 0.30f});
	}
	else
	{
		// One crawl cycle: palm plant, weight shift, the long drag of trailing
		// legs, then a grit tail. Loops at the crawl cadence.
		DragNotes.Add({0.000f, 0.070f, 66.0f, 0.240f, 0.008f, 2.4f, EIGToneWaveform::Sine});
		DragNotes.Add({0.000f, 0.050f, 700.0f, 0.050f, 0.030f, 1.5f, EIGToneWaveform::ValueNoise});
		DragNotes.Add({0.180f, 0.520f, 320.0f, 0.085f, 0.180f, 1.2f, EIGToneWaveform::ValueNoise});
		DragNotes.Add({0.180f, 0.520f, 92.0f, 0.070f, 0.180f, 1.4f, EIGToneWaveform::ValueNoise});
		DragNotes.Add({0.740f, 0.180f, 1400.0f, 0.024f, 0.200f, 1.8f, EIGToneWaveform::ValueNoise});
		DragNotes.Add({0.180f, 0.520f, 240.0f, 0.060f, 0.180f, 1.3f, EIGToneWaveform::BandNoise, 0.40f});
		DragNotes.Add({0.200f, 0.500f, 500.0f, 0.045f, 0.200f, 1.2f, EIGToneWaveform::Crackle});
	}

	Wave->ConfigureNotes(MoveTemp(DragNotes), true, 1.05f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateEntityCrawlStep(
	UObject* Outer,
	const bool bVinyl)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		bVinyl ? TEXT("IGEntityCrawlStepVinyl") : TEXT("IGEntityCrawlStep"));
	TArray<FIGToneNote> Notes;
	if (bVinyl)
	{
		// 장판: 손바닥이 미끄러지며 찍, 무릎이 눌리는 낮은 톡.
		Notes.Add({0.000f, 0.060f, 78.0f, 0.220f, 0.008f, 2.6f, EIGToneWaveform::Sub});
		Notes.Add({0.000f, 0.035f, 1100.0f, 0.110f, 0.05f, 1.8f, EIGToneWaveform::BandNoise, 0.25f});
		Notes.Add({0.040f, 0.140f, 1750.0f, 0.050f, 0.20f, 1.6f, EIGToneWaveform::Pluck, 0.40f});
		Notes.Add({0.170f, 0.050f, 96.0f, 0.140f, 0.010f, 2.8f, EIGToneWaveform::Sub});
	}
	else
	{
		// 타일: 손바닥이 치고, 살이 끌리고, 모래가 갈리고, 무릎이 닿는다.
		Notes.Add({0.000f, 0.060f, 74.0f, 0.240f, 0.008f, 2.6f, EIGToneWaveform::Sub});
		Notes.Add({0.000f, 0.035f, 900.0f, 0.120f, 0.05f, 1.8f, EIGToneWaveform::BandNoise, 0.25f});
		Notes.Add({0.040f, 0.160f, 1500.0f, 0.070f, 0.20f, 1.4f, EIGToneWaveform::BandNoise, 0.30f});
		Notes.Add({0.170f, 0.050f, 92.0f, 0.150f, 0.010f, 2.8f, EIGToneWaveform::Sub});
		Notes.Add({0.180f, 0.100f, 600.0f, 0.060f, 0.20f, 1.4f, EIGToneWaveform::Crackle});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateEntityBreathLoop(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGEntityBreathLoop"));
	constexpr float LoopSeconds = 3.2f;
	TArray<FIGToneNote> Notes;
	// 들이쉬는 숨은 높고 길게 차오르고, 내쉬는 숨은 낮고 목이 울린다. 마지막에
	// 젖은 딸깍 하나. 사람의 숨인데 사람의 숨이 아니다.
	Notes.Add({0.00f, 1.10f, 620.0f, 0.090f, 0.60f, 1.2f, EIGToneWaveform::BandNoise, 0.30f});
	Notes.Add({1.30f, 1.50f, 380.0f, 0.110f, 0.30f, 1.0f, EIGToneWaveform::BandNoise, 0.35f});
	Notes.Add({1.35f, 1.20f, 58.0f, 0.085f, 0.40f, 1.0f, EIGToneWaveform::Growl, 0.30f});
	Notes.Add({2.75f, 0.03f, 2400.0f, 0.045f, 0.10f, 1.0f, EIGToneWaveform::WhiteNoise});
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopSeconds);
	Wave->ConfigurePitchWow(0.006f, 0.19f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateDarknessInhale(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGDarknessInhale"));
	TArray<FIGToneNote> Notes;
	// 목소리 없는 바람이 길게 차오른다. 그 밑에 귀보다 몸으로 듣는 저음.
	Notes.Add({0.00f, 1.60f, 240.0f, 0.100f, 0.75f, 1.0f, EIGToneWaveform::BandNoise, 0.25f});
	Notes.Add({0.10f, 1.50f, 420.0f, 0.060f, 0.70f, 1.2f, EIGToneWaveform::BandNoise, 0.35f});
	Notes.Add({0.20f, 1.40f, 38.0f, 0.050f, 0.60f, 1.0f, EIGToneWaveform::Sub});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateDarknessBreathLoop(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGDarknessBreathLoop"));
	constexpr float LoopSeconds = 3.6f;
	TArray<FIGToneNote> Notes;
	// 위층 사람의 숨보다 느리고 목이 울리지 않는다. 사람의 숨이 아니라 방이 숨 쉰다.
	Notes.Add({0.00f, 1.40f, 260.0f, 0.080f, 0.60f, 1.1f, EIGToneWaveform::BandNoise, 0.30f});
	Notes.Add({1.70f, 1.60f, 170.0f, 0.070f, 0.25f, 1.0f, EIGToneWaveform::BandNoise, 0.30f});
	Notes.Add({0.00f, 3.60f, 33.0f, 0.035f, 0.30f, 0.8f, EIGToneWaveform::Sub});
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopSeconds);
	Wave->ConfigurePitchWow(0.004f, 0.12f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateEntityAlertVocal(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGEntityAlertVocal"));
	TArray<FIGToneNote> Notes;
	// 날카로운 들숨(어택이 긴 잡음), 그 뒤 낮은 으르렁 둘과 목의 덜컹.
	Notes.Add({0.00f, 0.35f, 900.0f, 0.200f, 0.85f, 0.6f, EIGToneWaveform::BandNoise, 0.40f});
	Notes.Add({0.30f, 0.60f, 70.0f, 0.240f, 0.15f, 1.6f, EIGToneWaveform::Growl, 0.55f});
	Notes.Add({0.30f, 0.50f, 105.0f, 0.110f, 0.20f, 1.6f, EIGToneWaveform::Growl, 0.40f});
	Notes.Add({0.35f, 0.50f, 40.0f, 0.090f, 0.20f, 1.4f, EIGToneWaveform::Crackle});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateEntityChaseScream(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGEntityChaseScream"));
	TArray<FIGToneNote> Notes;
	// 추격이 시작되는 소리는 그의 몸에서 난다. 두 손바닥이 연달아 바닥을 치고,
	// 굳은 미장이 갈라지고, 끌림이 한꺼번에 밀려온다. 0.55초에 저역이 떨어지며
	// 불협 세 줄이 튕긴다. 목소리는 없다(§4.6). 녹음 Stinger_ChaseStart가 없을 때
	// 이 소리가 대신 난다.
	Notes.Add({0.000f, 0.060f, 74.0f, 0.280f, 0.008f, 2.6f, EIGToneWaveform::Sub});
	Notes.Add({0.000f, 0.035f, 900.0f, 0.150f, 0.05f, 1.8f, EIGToneWaveform::BandNoise, 0.25f});
	Notes.Add({0.140f, 0.060f, 66.0f, 0.260f, 0.008f, 2.6f, EIGToneWaveform::Sub});
	Notes.Add({0.140f, 0.035f, 950.0f, 0.140f, 0.05f, 1.8f, EIGToneWaveform::BandNoise, 0.25f});
	Notes.Add({0.050f, 0.012f, 1900.0f, 0.060f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.110f, 0.012f, 2600.0f, 0.050f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.190f, 0.012f, 3100.0f, 0.045f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.260f, 0.012f, 2200.0f, 0.050f, 0.05f, 1.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.200f, 0.700f, 320.0f, 0.090f, 0.25f, 1.2f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.300f, 0.600f, 520.0f, 0.060f, 0.20f, 1.2f, EIGToneWaveform::BandNoise, 0.35f});
	Notes.Add({0.55f, 0.35f, 44.0f, 0.260f, 0.01f, 2.8f, EIGToneWaveform::Sub});
	Notes.Add({0.55f, 0.90f, 220.0f, 0.060f, 0.01f, 1.6f, EIGToneWaveform::Pluck, 0.80f});
	Notes.Add({0.55f, 0.90f, 233.0f, 0.060f, 0.01f, 1.6f, EIGToneWaveform::Pluck, 0.80f});
	Notes.Add({0.55f, 0.90f, 247.0f, 0.060f, 0.01f, 1.6f, EIGToneWaveform::Pluck, 0.80f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	Wave->ConfigureRoomTail(0.024f, 0.35f, 0.30f, 0.18f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCloseCallStinger(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGCloseCallStinger"));
	TArray<FIGToneNote> Notes;
	// 백색 타격 한 방, 떨어지는 저역, 불협 세 줄, 뒤따르는 잡음 밀물. 0.8초.
	Notes.Add({0.00f, 0.06f, 3600.0f, 0.220f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.00f, 0.50f, 46.0f, 0.300f, 0.01f, 2.4f, EIGToneWaveform::Sub});
	Notes.Add({0.00f, 0.70f, 233.0f, 0.070f, 0.01f, 1.6f, EIGToneWaveform::Pluck, 0.75f});
	Notes.Add({0.00f, 0.70f, 247.0f, 0.070f, 0.01f, 1.6f, EIGToneWaveform::Pluck, 0.75f});
	Notes.Add({0.00f, 0.70f, 262.0f, 0.070f, 0.01f, 1.6f, EIGToneWaveform::Pluck, 0.75f});
	Notes.Add({0.05f, 0.60f, 1400.0f, 0.120f, 0.50f, 1.2f, EIGToneWaveform::BandNoise, 0.35f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCaptureLunge(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGCaptureLunge"));
	TArray<FIGToneNote> Notes;
	// 덮치는 순간: 타격, 저역, 천이 스치고, 목이 운다. 0.6초. 그 뒤가 드라이 노크 둘이다.
	Notes.Add({0.00f, 0.05f, 2600.0f, 0.240f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.00f, 0.40f, 38.0f, 0.340f, 0.01f, 2.6f, EIGToneWaveform::Sub});
	Notes.Add({0.02f, 0.35f, 700.0f, 0.160f, 0.10f, 1.6f, EIGToneWaveform::BandNoise, 0.30f});
	Notes.Add({0.05f, 0.30f, 80.0f, 0.140f, 0.10f, 1.6f, EIGToneWaveform::Growl, 0.50f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCaptureStruggle(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGCaptureStruggle"));
	TArray<FIGToneNote> Notes;
	// 접촉 뒤 목 가까이의 마찰과 그의 숨, 끊기는 숨, 그리고 암전 속의 노크 둘.
	// 노크 두 번은 CreateWallKnockReply와 재료도 간격(0.42초)도 같다. 밤3에 벽이
	// 돌려주는 대답은 여기서 먼저 들은 소리여야 한다. 둘째 노크의 꼬리는 2.10초에
	// 닫힌다 — 2.15초에 침대로 옮겨지면 이 음원은 복도에 남는다.
	Notes.Add({.16f, .38f, 780.f, .14f, .05f, 1.5f, EIGToneWaveform::BandNoise, .4f});
	Notes.Add({.30f, .80f, 620.f, .08f, .45f, 1.2f, EIGToneWaveform::BandNoise, .30f});
	Notes.Add({.72f, .22f, 1400.f, .11f, .02f, 2.2f, EIGToneWaveform::BandNoise, .3f});
	Notes.Add({1.02f, .36f, 43.f, .13f, .04f, 2.8f, EIGToneWaveform::Sub});
	Notes.Add({1.06f, .32f, 530.f, .12f, .13f, 1.7f, EIGToneWaveform::BandNoise, .65f});
	Notes.Add({1.48f, .100f, 58.f, .250f, .005f, 2.8f, EIGToneWaveform::Sub});
	Notes.Add({1.48f, .050f, 176.f, .060f, .008f, 2.0f, EIGToneWaveform::Sine});
	Notes.Add({1.48f, .024f, 1000.f, .030f, .030f, 1.4f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.48f, .003f, 3600.f, .040f, .050f, 1.0f, EIGToneWaveform::WhiteNoise});
	Notes.Add({1.48f, .200f, 196.f, .090f, .004f, 2.2f, EIGToneWaveform::Pluck, .38f});
	Notes.Add({1.90f, .100f, 58.f, .250f, .005f, 2.8f, EIGToneWaveform::Sub});
	Notes.Add({1.90f, .050f, 176.f, .060f, .008f, 2.0f, EIGToneWaveform::Sine});
	Notes.Add({1.90f, .024f, 1000.f, .030f, .030f, 1.4f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.90f, .003f, 3600.f, .040f, .050f, 1.0f, EIGToneWaveform::WhiteNoise});
	Notes.Add({1.90f, .200f, 196.f, .090f, .004f, 2.2f, EIGToneWaveform::Pluck, .38f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCaptureCut(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGCaptureCut"));
	TArray<FIGToneNote> Notes;
	// 화면이 검게 끊기는 순간 머리가 바닥에 닿는 둔한 타격이 온다. 그 뒤로 귀에
	// 높은 울림이 남는다. 두 음을 67Hz 어긋나게 겹쳐 맥놀이가 나고 1.5초에 걸쳐
	// 사라진다. 그 아래로 몸싸움 소리의 뒤쪽과 노크 둘이 이어진다.
	Notes.Add({0.00f, 0.45f, 44.0f, 0.340f, 0.004f, 2.6f, EIGToneWaveform::Sub});
	Notes.Add({0.00f, 0.08f, 900.0f, 0.100f, 0.010f, 1.6f, EIGToneWaveform::BandNoise, 0.35f});
	Notes.Add({0.04f, 1.55f, 6120.0f, 0.020f, 0.100f, 1.2f, EIGToneWaveform::Sine});
	Notes.Add({0.04f, 1.55f, 6187.0f, 0.016f, 0.100f, 1.2f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePresenceLayer(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGPresenceLayer"));
	constexpr float LoopSeconds = 6.0f;
	TArray<FIGToneNote> Notes;
	// 36Hz 저역과 가는 휘파람, 그 사이 느린 목울림. 소리 자체는 작고 볼륨은
	// 오디오 감독이 존재 거리로 올린다.
	Notes.Add({0.0f, LoopSeconds, 36.0f, 0.100f, 0.50f, 0.4f, EIGToneWaveform::Sub});
	Notes.Add({0.0f, LoopSeconds, 2400.0f, 0.028f, 0.50f, 0.4f, EIGToneWaveform::BandNoise, 0.90f});
	Notes.Add({1.0f, 4.0f, 48.0f, 0.060f, 0.50f, 0.8f, EIGToneWaveform::Growl, 0.25f});
	Notes.Add({0.0f, LoopSeconds, 120.0f, 0.020f, 0.50f, 0.5f, EIGToneWaveform::BandNoise, 0.60f});
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopSeconds);
	Wave->ConfigurePitchWow(0.004f, 0.09f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateSettlePipeKnock(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGSettlePipeKnock"));
	TArray<FIGToneNote> Notes;
	// 배관이 열을 먹고 한 번 튄다. 금속 공명 하나와 저역, 접촉.
	Notes.Add({0.00f, 0.004f, 3000.0f, 0.120f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.00f, 0.50f, 180.0f, 0.240f, 0.005f, 2.2f, EIGToneWaveform::Pluck, 0.55f});
	Notes.Add({0.00f, 0.12f, 55.0f, 0.200f, 0.010f, 2.6f, EIGToneWaveform::Sub});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	Wave->ConfigureRoomTail(0.030f, 0.45f, 0.40f, 0.28f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateSettleTimberCreak(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGSettleTimberCreak"));
	TArray<FIGToneNote> Notes;
	// 목재가 뒤틀리며 운다. 좁은 대역 둘이 엇갈리고 낮은 목울림이 받친다.
	Notes.Add({0.00f, 0.35f, 420.0f, 0.140f, 0.30f, 1.2f, EIGToneWaveform::BandNoise, 0.85f});
	Notes.Add({0.05f, 0.40f, 610.0f, 0.100f, 0.40f, 1.2f, EIGToneWaveform::BandNoise, 0.85f});
	Notes.Add({0.10f, 0.30f, 250.0f, 0.080f, 0.30f, 1.0f, EIGToneWaveform::Growl, 0.20f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateSettleFarDoorSlam(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGSettleFarDoorSlam"));
	TArray<FIGToneNote> Notes;
	// 멀리서 문이 닫힌다. 접촉은 짧고 저역이 길고 건물이 되운다.
	Notes.Add({0.00f, 0.006f, 2200.0f, 0.140f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.00f, 0.22f, 52.0f, 0.280f, 0.010f, 2.6f, EIGToneWaveform::Sub});
	Notes.Add({0.01f, 0.15f, 300.0f, 0.130f, 0.05f, 2.0f, EIGToneWaveform::BandNoise, 0.30f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	Wave->ConfigureRoomTail(0.038f, 0.50f, 0.30f, 0.26f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateSettlePlasterTick(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGSettlePlasterTick"));
	TArray<FIGToneNote> Notes;
	// 석고가 갈라지며 알갱이가 떨어진다. 짧다.
	Notes.Add({0.00f, 0.03f, 4000.0f, 0.100f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	Notes.Add({0.00f, 0.20f, 500.0f, 0.160f, 0.05f, 1.6f, EIGToneWaveform::Crackle});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateFrottageRub(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGFrottageRub"));
	TArray<FIGToneNote> RubNotes;

	// §21.3 fixes the band at 900~4200 Hz: graphite on paper has no low end and
	// no pitch, only the grain of the sheet. Three strokes to the loop at about
	// two and a half a second, which is the pace of a hand pressing hard enough
	// to lift letters rather than sketching.
	constexpr float LoopSeconds = 1.20f;
	const float StrokeStarts[] = {0.000f, 0.405f, 0.798f};
	for (int32 StrokeIndex = 0; StrokeIndex < UE_ARRAY_COUNT(StrokeStarts); ++StrokeIndex)
	{
		const float Start = StrokeStarts[StrokeIndex];
		// Each stroke swells and dies: the middle of a stroke presses hardest,
		// and the turn at either end is nearly silent. A flat band would read as
		// a machine, not a hand.
		RubNotes.Add({Start, 0.360f, 1200.0f, 0.062f, 0.300f, 1.5f, EIGToneWaveform::ValueNoise});
		RubNotes.Add({Start + 0.020f, 0.320f, 2600.0f, 0.044f, 0.320f, 1.6f, EIGToneWaveform::ValueNoise});
		RubNotes.Add({Start + 0.045f, 0.270f, 4200.0f, 0.026f, 0.340f, 1.8f, EIGToneWaveform::ValueNoise});
		// The sheet's own body under the pressure, at the bottom of the band.
		RubNotes.Add({Start, 0.300f, 900.0f, 0.034f, 0.280f, 1.4f, EIGToneWaveform::ValueNoise});
		// The graphite catching on the weave once per stroke, never on the beat.
		RubNotes.Add({
			Start + 0.150f + 0.030f * StrokeIndex,
			0.014f,
			3400.0f,
			0.020f,
			0.010f,
			1.4f,
			EIGToneWaveform::ValueNoise});
	}

	Wave->ConfigureNotes(MoveTemp(RubNotes), true, LoopSeconds);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateAudibleHeartbeat(
	UObject* Outer,
	const float Loudness)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGAudibleHeartbeat"));
	TArray<FIGToneNote> Beat;

	// §21.3: 기존 심박 + 220Hz 로우패스, −6dB, 박동 동기. The timing is exactly
	// the 2D beat's so the change is heard as the *same* heart, not a new sound.
	// −6 dB is half the amplitude; the low pass shows up as the 88 Hz partial
	// leaving, which is the part that made the pulse sound crisp and close.
	const float Safe = FMath::Clamp(Loudness, 0.0f, 1.0f) * 0.5f;
	Beat.Add({0.0f, 0.19f, 44.0f, Safe, 0.05f, 2.2f, EIGToneWaveform::Sine});
	Beat.Add({0.20f, 0.16f, 38.0f, Safe * 0.72f, 0.06f, 2.4f, EIGToneWaveform::Sine});
	// Conducted through a chest and a wall it loses the transient and gains a
	// little body: it rings slightly longer than the one inside your head.
	Beat.Add({0.0f, 0.24f, 62.0f, Safe * 0.26f, 0.09f, 1.9f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(Beat), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePlayerBreathLoop(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGPlayerBreathLoop"));
	constexpr float LoopSeconds = 4.2f;
	TArray<FIGToneNote> Notes;
	// 들숨은 이 사이로 새는 고역이 얹힌 대역 잡음, 날숨은 목이 조금 울린다.
	// 두 번째 짝은 짧고 급하다 — 같은 숨이 두 번 반복되면 기계 소리다.
	// 유담은 스물일곱 살 여자다. 숨의 대역은 남자 숨보다 18% 남짓 높고, 날숨에 섞이는
	// 목소리는 220 Hz 근처다. 110 Hz로 두면 숨만 들어도 남자로 들린다.
	Notes.Add({0.00f, 1.05f, 1050.0f, 0.200f, 0.55f, 1.3f, EIGToneWaveform::BandNoise, 0.22f});
	Notes.Add({0.10f, 0.90f, 1850.0f, 0.080f, 0.60f, 1.2f, EIGToneWaveform::BandNoise, 0.35f});
	Notes.Add({1.30f, 1.20f, 620.0f, 0.220f, 0.25f, 1.1f, EIGToneWaveform::BandNoise, 0.20f});
	Notes.Add({1.35f, 0.80f, 220.0f, 0.035f, 0.30f, 1.0f, EIGToneWaveform::Sub});
	Notes.Add({2.75f, 0.70f, 1150.0f, 0.170f, 0.50f, 1.3f, EIGToneWaveform::BandNoise, 0.22f});
	Notes.Add({3.50f, 0.68f, 640.0f, 0.190f, 0.25f, 1.1f, EIGToneWaveform::BandNoise, 0.20f});
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopSeconds);
	Wave->ConfigurePitchWow(0.010f, 0.23f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePlayerGasp(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGPlayerGasp"));
	TArray<FIGToneNote> Notes;
	// 공기를 한 번에 들이켠다. 끝에서 성문이 닫히는 고역과 목의 딸깍.
	// 들이켜는 순간 걸리는 목소리는 250 Hz 언저리의 짧은 '흡'이다(여자 목소리).
	Notes.Add({0.00f, 0.42f, 1650.0f, 0.270f, 0.18f, 1.5f, EIGToneWaveform::BandNoise, 0.30f});
	Notes.Add({0.02f, 0.36f, 850.0f, 0.180f, 0.22f, 1.4f, EIGToneWaveform::BandNoise, 0.25f});
	Notes.Add({0.04f, 0.22f, 250.0f, 0.035f, 0.12f, 1.8f, EIGToneWaveform::Triangle});
	Notes.Add({0.30f, 0.14f, 3000.0f, 0.080f, 0.10f, 2.0f, EIGToneWaveform::BandNoise, 0.45f});
	Notes.Add({0.41f, 0.012f, 3400.0f, 0.080f, 0.10f, 1.0f, EIGToneWaveform::WhiteNoise});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePlayerExhale(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGPlayerExhale"));
	TArray<FIGToneNote> Notes;
	// 한숨. 대역 잡음에 목소리가 살짝 섞이고, 날숨이 세 번 끊긴다 — 떨림이다.
	// 섞이는 목소리는 205 Hz. 녹음이 없는 소리라 여기서 음역을 정해야 한다.
	Notes.Add({0.00f, 0.95f, 570.0f, 0.220f, 0.10f, 1.7f, EIGToneWaveform::BandNoise, 0.18f});
	Notes.Add({0.00f, 0.80f, 360.0f, 0.150f, 0.12f, 1.6f, EIGToneWaveform::BandNoise, 0.22f});
	Notes.Add({0.02f, 0.55f, 205.0f, 0.035f, 0.15f, 1.5f, EIGToneWaveform::Sub});
	Notes.Add({0.34f, 0.07f, 730.0f, 0.090f, 0.20f, 1.2f, EIGToneWaveform::BandNoise, 0.30f});
	Notes.Add({0.56f, 0.07f, 710.0f, 0.080f, 0.20f, 1.2f, EIGToneWaveform::BandNoise, 0.30f});
	Notes.Add({0.78f, 0.08f, 660.0f, 0.070f, 0.20f, 1.2f, EIGToneWaveform::BandNoise, 0.30f});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateMenuTick(UObject* Outer, const bool bConfirm)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(Outer, TEXT("IGMenuTick"));
	TArray<FIGToneNote> Notes;
	if (bConfirm)
	{
		// 둘. 두 번째가 오분의 일 위라 「됐다」로 읽힌다.
		Notes.Add({0.000f, 0.090f, 880.0f, 0.220f, 0.01f, 2.2f, EIGToneWaveform::Pluck, 0.30f});
		Notes.Add({0.055f, 0.090f, 1320.0f, 0.170f, 0.01f, 2.2f, EIGToneWaveform::Pluck, 0.30f});
		Notes.Add({0.000f, 0.006f, 4000.0f, 0.070f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	}
	else
	{
		Notes.Add({0.000f, 0.060f, 1180.0f, 0.200f, 0.01f, 2.4f, EIGToneWaveform::Pluck, 0.25f});
		Notes.Add({0.000f, 0.005f, 4000.0f, 0.060f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePlasterSettle(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGPlasterSettle"));
	TArray<FIGToneNote> SettleNotes;

	// Dry hairline cracks with no resonance: hardened plaster, not wood.
	SettleNotes.Add({0.000f, 0.030f, 2400.0f, 0.060f, 0.010f, 1.0f, EIGToneWaveform::ValueNoise});
	SettleNotes.Add({0.140f, 0.024f, 3100.0f, 0.045f, 0.010f, 1.0f, EIGToneWaveform::ValueNoise});
	SettleNotes.Add({0.330f, 0.040f, 1900.0f, 0.050f, 0.010f, 1.2f, EIGToneWaveform::ValueNoise});
	SettleNotes.Add({0.330f, 0.060f, 120.0f, 0.060f, 0.010f, 2.6f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(SettleNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePlasterDustFall(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGPlasterDustFall"));
	TArray<FIGToneNote> DustNotes;

	// §21.3 fixes this cue at a 6 kHz high pass with an exponential decay over
	// 0.90 s, and that discipline is right: falling grains answer broadband with
	// a spectral centroid that climbs as they get finer, and plaster powder is
	// far finer than sand. Everything here therefore sits above 6 kHz. Anything
	// lower would be debris, and debris already has its own cue (미장 갈라짐).
	// Three overlapping bands with high release powers approximate the decay.
	DustNotes.Add({0.000f, 0.900f, 6200.0f, 0.030f, 0.020f, 3.2f, EIGToneWaveform::ValueNoise});
	DustNotes.Add({0.040f, 0.800f, 7100.0f, 0.024f, 0.060f, 2.8f, EIGToneWaveform::ValueNoise});
	DustNotes.Add({0.120f, 0.720f, 8400.0f, 0.016f, 0.100f, 3.0f, EIGToneWaveform::ValueNoise});

	// Individual grains riding the sift. Irregular on purpose — even spacing
	// would read as a machine ticking somewhere in the building.
	const float GrainStarts[] = {0.031f, 0.118f, 0.207f, 0.264f, 0.415f, 0.596f, 0.742f};
	const float GrainBands[] = {6900.0f, 8100.0f, 7300.0f, 9200.0f, 6400.0f, 8700.0f, 7600.0f};
	for (int32 GrainIndex = 0; GrainIndex < UE_ARRAY_COUNT(GrainStarts); ++GrainIndex)
	{
		// 8–20 ms each: one grain of granular synthesis, which is exactly what
		// a single falling speck is.
		const float GrainSeconds = 0.008f + 0.012f * ((GrainIndex % 3) * 0.5f);
		const float GrainAmplitude = 0.018f - 0.0011f * GrainIndex;
		DustNotes.Add({
			GrainStarts[GrainIndex],
			GrainSeconds,
			GrainBands[GrainIndex],
			GrainAmplitude,
			0.010f,
			1.3f,
			EIGToneWaveform::ValueNoise});
	}

	Wave->ConfigureNotes(MoveTemp(DustNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateBoardStackFall(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGBoardStackFall"));
	TArray<FIGToneNote> Notes;
	// 판면이 바닥을 한꺼번에 때리는 넓은 찰싹. 문짝과 달리 걸쇠 딸깍이 없다.
	Notes.Add({0.000f, 0.006f, 3200.0f, 0.100f, 0.05f, 1.0f, EIGToneWaveform::WhiteNoise});
	// 판 여러 장의 무게가 실린 저역.
	Notes.Add({0.000f, 0.180f, 58.0f, 0.240f, 0.004f, 2.6f, EIGToneWaveform::Sub});
	// 석고 판이 우는 넓은 대역. 나무 문짝보다 둔하고 짧다.
	Notes.Add({0.004f, 0.220f, 420.0f, 0.140f, 0.01f, 2.0f, EIGToneWaveform::BandNoise, 0.25f});
	Notes.Add({0.010f, 0.090f, 1400.0f, 0.070f, 0.01f, 2.2f, EIGToneWaveform::BandNoise, 0.20f});
	// 부서진 모서리에서 떨어지는 부스러기.
	Notes.Add({0.060f, 0.400f, 900.0f, 0.050f, 0.05f, 1.6f, EIGToneWaveform::Crackle});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	Wave->ConfigureRoomTail(0.018f, 0.34f, 0.30f, 0.18f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateRooftopTankSlosh(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGRooftopTankSlosh"));
	// 4.4초에 한 번 왕복한다. 가득 찬 탱크일수록 느리게 오간다.
	constexpr float LoopLength = 8.8f;
	TArray<FIGToneNote> Notes;
	// 수면 아래에서 움직이는 덩어리. 대역이 아주 좁고 끊기지 않는다.
	Notes.Add({0.0f, LoopLength, 33.0f, 0.085f, 0.40f, 0.7f, EIGToneWaveform::Sine});
	Notes.Add({0.0f, LoopLength, 210.0f, 0.016f, 0.45f, 0.8f, EIGToneWaveform::ValueNoise});
	for (int32 Sway = 0; Sway < 2; ++Sway)
	{
		const float Start = Sway * 4.4f;
		// 밀려갔다 돌아오는 물. 올라갈 때가 길고 내려올 때가 짧다.
		Notes.Add({Start + 0.30f, 2.10f, 128.0f, 0.030f, 0.55f, 1.1f, EIGToneWaveform::ValueNoise});
		Notes.Add({Start + 2.20f, 1.40f, 96.0f, 0.024f, 0.35f, 1.4f, EIGToneWaveform::ValueNoise});
		// 돌아온 물이 강판을 친다. 판이 얇아 배음이 남는다.
		Notes.Add({Start + 3.55f, 0.075f, 340.0f, 0.052f, 0.010f, 2.6f, EIGToneWaveform::ValueNoise});
		Notes.Add({Start + 3.55f, 0.760f, 152.0f, 0.040f, 0.008f, 1.5f, EIGToneWaveform::Triangle});
		Notes.Add({Start + 3.55f, 0.540f, 421.0f, 0.017f, 0.008f, 1.7f, EIGToneWaveform::Triangle});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopLength);
	Wave->ConfigurePitchWow(0.0024f, 0.045f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateVacantUnitTone(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGVacantUnitTone"));
	TArray<FIGToneNote> Notes;
	// 방의 공기. 아주 넓고 아주 작다 — 벽이 있다는 것 말고는 아무 정보가
	// 없는 소리다.
	Notes.Add({0.00f, 3.20f, 640.0f, 0.012f, 0.50f, 0.8f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.00f, 3.20f, 180.0f, 0.009f, 0.55f, 0.8f, EIGToneWaveform::ValueNoise});
	// 문틈으로 드는 복도 공기. 이것만 있고 그 아래가 비어 있다.
	Notes.Add({0.35f, 1.40f, 2100.0f, 0.007f, 0.40f, 1.2f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.95f, 1.05f, 1750.0f, 0.005f, 0.45f, 1.3f, EIGToneWaveform::ValueNoise});
	// 60Hz도 120Hz도 없다. 그 부재가 이 큐의 전부이므로 저역에 아무것도
	// 넣지 않는다 — 여기에 한 줄이라도 더하면 큐의 뜻이 사라진다.
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateRoofDoorGust(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGRoofDoorGust"));
	TArray<FIGToneNote> Notes;
	// 돌풍 둘. 길이가 서로 다르고 정점도 어긋난다 — 바람에는 박자가 없다는
	// 것이 이 큐가 하는 유일한 말이다.
	Notes.Add({0.00f, 1.90f, 900.0f, 0.070f, 0.42f, 1.1f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.10f, 1.70f, 320.0f, 0.048f, 0.45f, 1.2f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.65f, 2.30f, 1150.0f, 0.055f, 0.38f, 1.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.80f, 2.10f, 260.0f, 0.040f, 0.44f, 1.1f, EIGToneWaveform::ValueNoise});
	// 철문 틈에서 나는 얇은 휘파람. 돌풍이 셀 때만 선다.
	Notes.Add({0.55f, 0.80f, 2650.0f, 0.016f, 0.30f, 1.6f, EIGToneWaveform::Sine});
	Notes.Add({2.25f, 0.95f, 2410.0f, 0.014f, 0.32f, 1.6f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	Wave->ConfigurePitchWow(0.0090f, 0.23f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateFoamedRoomHum(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGFoamedRoomHum"));
	TArray<FIGToneNote> Notes;
	// 92Hz와 배음 셋. 계란판이 고역을 먹으므로 이 위로는 아무것도 없다. 92Hz만
	// 서 있으면 노트북이나 TV 스피커에서는 거의 안 들려서, 배음을 올려 작은
	// 스피커에서도 도는 기계로 잡히게 한다.
	Notes.Add({0.00f, 4.00f, 92.0f, 0.052f, 0.35f, 0.9f, EIGToneWaveform::Sine});
	Notes.Add({0.00f, 4.00f, 184.0f, 0.030f, 0.40f, 0.9f, EIGToneWaveform::Sine});
	Notes.Add({0.00f, 4.00f, 276.0f, 0.016f, 0.45f, 1.0f, EIGToneWaveform::Triangle});
	Notes.Add({0.00f, 4.00f, 368.0f, 0.008f, 0.45f, 1.0f, EIGToneWaveform::Sine});
	// 기계가 한 번 부하를 받는다. 사람이 쓰는 방이라는 유일한 신호다.
	Notes.Add({2.10f, 0.70f, 92.0f, 0.018f, 0.20f, 1.4f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	Wave->ConfigurePitchWow(0.0018f, 0.09f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateMachineHumLoop(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGMachineHumLoop"));
	constexpr float LoopSeconds = 3.60f;
	TArray<FIGToneNote> Notes;
	// 상용 전원 60Hz와 배음 둘. 엔딩 B가 냉장고를 이 음으로 깔아 놨으니
	// 배전반과 보일러도 같은 배선에 물린 소리로 들린다.
	Notes.Add({0.00f, LoopSeconds, 60.0f, 0.052f, 0.30f, 0.20f, EIGToneWaveform::Sine});
	Notes.Add({0.00f, LoopSeconds, 120.0f, 0.026f, 0.30f, 0.22f, EIGToneWaveform::Sine});
	Notes.Add({0.00f, LoopSeconds, 180.0f, 0.011f, 0.30f, 0.25f, EIGToneWaveform::Triangle});
	// 압축기가 한 번 부하를 문다. 순수 사인만 깔면 기계가 아니라 이명으로
	// 들려서 플레이어가 소리를 껐는지 의심한다.
	Notes.Add({1.90f, 1.05f, 60.0f, 0.020f, 0.35f, 1.30f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopSeconds);
	// 모터는 정확한 주파수로 안 돈다. 얕게 흔들어야 기계로 읽힌다.
	Wave->ConfigurePitchWow(0.0021f, 0.07f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePoliceLineTapePull(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpiloguePoliceLineTape"));
	TArray<FIGToneNote> Notes;
	// 롤이 도는 동안 접착면이 계속 뜯긴다 — 끊기지 않는 마찰이 먼저고,
	// 그 위에 롤 축의 얇은 떨림이 얹힌다.
	Notes.Add({0.000f, 1.35f, 3100.0f, 0.052f, 0.060f, 1.1f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.040f, 1.24f, 1450.0f, 0.030f, 0.080f, 1.2f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.060f, 1.18f, 214.0f, 0.014f, 0.120f, 1.4f, EIGToneWaveform::Triangle});
	// 끝에서 손으로 끊는다.
	Notes.Add({1.36f, 0.075f, 4200.0f, 0.085f, 0.008f, 2.9f, EIGToneWaveform::ValueNoise});
	Notes.Add({1.36f, 0.140f, 620.0f, 0.036f, 0.012f, 2.4f, EIGToneWaveform::ValueNoise});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateGurneyWheels(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpilogueGurneyWheels"));
	TArray<FIGToneNote> Notes;
	// 작은 캐스터의 연속 구름. 타일 이음매를 네 번 넘고, 넘을 때마다
	// 조금씩 작아진다 — 멀어지는 것은 속도가 아니라 거리다.
	Notes.Add({0.000f, 3.10f, 1750.0f, 0.030f, 0.140f, 1.0f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.000f, 3.10f, 118.0f, 0.020f, 0.180f, 1.0f, EIGToneWaveform::Sine});
	const float SeamTimes[] = {0.42f, 1.16f, 1.94f, 2.71f};
	const float SeamGains[] = {1.00f, 0.82f, 0.64f, 0.47f};
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const float Start = SeamTimes[Index];
		const float Gain = SeamGains[Index];
		Notes.Add({Start, 0.055f, 96.0f, 0.090f * Gain, 0.006f, 2.6f, EIGToneWaveform::Sine});
		Notes.Add({Start, 0.038f, 2600.0f, 0.048f * Gain, 0.006f, 3.1f, EIGToneWaveform::ValueNoise});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCameraShutterTriple(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpilogueCameraShutter"));
	TArray<FIGToneNote> Notes;
	for (const float Start : {0.00f, 0.86f, 1.79f})
	{
		// 미러 슬랩, 셔터막, 그리고 감기 모터의 짧은 회전.
		Notes.Add({Start, 0.030f, 1250.0f, 0.115f, 0.004f, 3.4f, EIGToneWaveform::ValueNoise});
		Notes.Add({Start + 0.012f, 0.026f, 3400.0f, 0.080f, 0.004f, 3.6f, EIGToneWaveform::ValueNoise});
		Notes.Add({Start + 0.044f, 0.150f, 320.0f, 0.036f, 0.010f, 2.2f, EIGToneWaveform::SoftSquare});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateDebrisSweep(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpilogueDebrisSweep"));
	TArray<FIGToneNote> Notes;
	// 세 번 쓴다. 획마다 앞으로 밀리는 알갱이가 늘어 대역이 낮아진다.
	const float StrokeStarts[] = {0.00f, 1.05f, 2.02f};
	const float StrokeBands[] = {5200.0f, 4400.0f, 3700.0f};
	for (int32 Index = 0; Index < 3; ++Index)
	{
		const float Start = StrokeStarts[Index];
		Notes.Add({Start, 0.62f, StrokeBands[Index], 0.062f, 0.070f, 1.6f, EIGToneWaveform::ValueNoise});
		Notes.Add({Start + 0.05f, 0.50f, 880.0f, 0.024f, 0.090f, 1.8f, EIGToneWaveform::ValueNoise});
		// 획 끝에서 조각이 무더기에 부딪힌다.
		Notes.Add({Start + 0.58f, 0.070f, 1900.0f, 0.040f, 0.008f, 2.8f, EIGToneWaveform::ValueNoise});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateEpilogueWorkshopScore(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpilogueWorkshopScore"));
	constexpr int32 StrikeCount = 8;
	constexpr float StrikeSpacing = 1.70f;
	TArray<FIGToneNote> Notes;
	// CreateTuningMotif과 같은 걸음으로 시작하되, 여기서는 마지막 타건이
	// 0센트에 닿는다. 게임 내내 -30에서 -16까지만 오던 그 음이다.
	for (int32 Strike = 0; Strike < StrikeCount; ++Strike)
	{
		const float Progress =
			static_cast<float>(Strike) / static_cast<float>(StrikeCount - 1);
		const float Cents = FMath::Lerp(-30.0f, 0.0f, Progress);
		const float Frequency = 220.0f * FMath::Pow(2.0f, Cents / 1200.0f);
		const float Start = Strike * StrikeSpacing;
		// 조율이 끝나 갈수록 세게 치지 않는다. 확인만 하면 되기 때문이다.
		const float Amplitude = FMath::Lerp(0.082f, 0.058f, Progress);
		Notes.Add({Start, 1.18f, Frequency, Amplitude, 0.025f, 2.0f, EIGToneWaveform::Triangle});
		Notes.Add({Start, 0.34f, Frequency * 2.0f, Amplitude * 0.21f, 0.030f, 2.8f, EIGToneWaveform::Triangle});
	}

	// 마지막 타건 뒤에 손을 떼고 한 번 눌러 본다 — 열린 5도, 길게.
	const float ChordStart = StrikeCount * StrikeSpacing + 0.65f;
	Notes.Add({ChordStart, 5.20f, 220.00f, 0.052f, 0.055f, 1.3f, EIGToneWaveform::Triangle});
	Notes.Add({ChordStart, 5.20f, 329.63f, 0.034f, 0.070f, 1.3f, EIGToneWaveform::Triangle});
	Notes.Add({ChordStart, 4.60f, 440.00f, 0.021f, 0.090f, 1.5f, EIGToneWaveform::Triangle});
	Notes.Add({ChordStart, 3.40f, 110.00f, 0.026f, 0.120f, 1.4f, EIGToneWaveform::Sine});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	// 나무 몸통과 사람 손이라 아주 작게 흔들린다. 기계로 들리면 안 된다.
	Wave->ConfigurePitchWow(0.0016f, 0.11f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateEpilogueAutumnBed(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpilogueAutumnBed"));
	constexpr float LoopLength = 12.0f;
	TArray<FIGToneNote> Notes;
	// 크레인 유압. 골목 하나 건너에서 나는 소리라 저역만 남는다.
	Notes.Add({0.0f, LoopLength, 38.0f, 0.055f, 0.30f, 0.7f, EIGToneWaveform::Sine});
	Notes.Add({0.0f, LoopLength, 260.0f, 0.014f, 0.35f, 0.9f, EIGToneWaveform::ValueNoise});
	// 붐이 한 번 내려앉는다.
	Notes.Add({4.30f, 1.10f, 62.0f, 0.048f, 0.060f, 1.8f, EIGToneWaveform::Sine});
	Notes.Add({4.30f, 0.35f, 740.0f, 0.020f, 0.020f, 2.6f, EIGToneWaveform::ValueNoise});
	// 401호 창턱의 라디오. 대역을 좁혀 말이 되지 않게 둔다.
	for (int32 Phrase = 0; Phrase < 5; ++Phrase)
	{
		const float Start = 0.90f + Phrase * 2.15f;
		const float Frequency = 430.0f + static_cast<float>(Phrase % 3) * 55.0f;
		Notes.Add({Start, 1.15f, Frequency, 0.017f, 0.140f, 1.5f, EIGToneWaveform::ValueNoise});
		Notes.Add({Start + 0.28f, 0.62f, Frequency * 1.5f, 0.008f, 0.180f, 1.7f, EIGToneWaveform::ValueNoise});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), true, LoopLength);
	Wave->ConfigurePitchWow(0.0030f, 0.06f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateKeyDropMetalBox(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpilogueKeyDropBox"));
	TArray<FIGToneNote> Notes;
	// 투입구를 지나 얇은 철판 바닥에 닿고, 링이 한 번 더 튄다.
	Notes.Add({0.000f, 0.055f, 2900.0f, 0.070f, 0.004f, 3.2f, EIGToneWaveform::ValueNoise});
	Notes.Add({0.030f, 0.320f, 1180.0f, 0.062f, 0.006f, 2.0f, EIGToneWaveform::Triangle});
	Notes.Add({0.030f, 0.280f, 1770.0f, 0.034f, 0.006f, 2.2f, EIGToneWaveform::Triangle});
	Notes.Add({0.034f, 0.400f, 176.0f, 0.038f, 0.008f, 1.9f, EIGToneWaveform::Sine});
	Notes.Add({0.155f, 0.190f, 2340.0f, 0.026f, 0.005f, 2.8f, EIGToneWaveform::Triangle});
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateRailingKnockTwo(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGEpilogueRailingKnockTwo"));
	TArray<FIGToneNote> Notes;
	// 「둘」의 간격은 §7 P4와 같은 0.34초다. 응답 노크를 배운 손이
	// 그대로 치는 것이므로 박자를 새로 만들지 않는다.
	for (const float Start : {0.00f, 0.34f})
	{
		Notes.Add({Start, 0.030f, 1900.0f, 0.075f, 0.004f, 3.0f, EIGToneWaveform::ValueNoise});
		// 강관은 벽과 달리 배음이 오래 남는다.
		Notes.Add({Start, 0.620f, 486.0f, 0.056f, 0.005f, 1.6f, EIGToneWaveform::Triangle});
		Notes.Add({Start, 0.520f, 1312.0f, 0.024f, 0.005f, 1.8f, EIGToneWaveform::Triangle});
		Notes.Add({Start, 0.240f, 92.0f, 0.030f, 0.006f, 2.4f, EIGToneWaveform::Sine});
	}
	Wave->ConfigureNotes(MoveTemp(Notes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateRecordingPlayback(
	UObject* Outer,
	const TArray<FIGRecordedSound>& Sounds)
{
	if (Sounds.Num() == 0)
	{
		return nullptr;
	}
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGRecordingPlayback"));
	TArray<FIGToneNote> TakeNotes;

	// Room tone of a cheap microphone left against a door: a thin hiss that runs
	// the whole take. It keeps playing through the gaps, which is exactly why the
	// gaps read as silence-on-a-recording rather than as the game stopping.
	float TakeEnd = 0.0f;
	for (const FIGRecordedSound& Sound : Sounds)
	{
		TakeEnd = FMath::Max(
			TakeEnd,
			Sound.OffsetSeconds + Sound.DurationSeconds);
	}
	TakeEnd = FMath::Max(TakeEnd, 1.0f);
	// 룸톤이 공백을 들리게 한다. 0.012/0.008이던 때는 발소리보다 16dB 넘게
	// 작아서, 공백이 테이프의 무음이 아니라 재생이 멈춘 것처럼 들렸다.
	TakeNotes.Add({0.0f, TakeEnd, 5200.0f, 0.022f, 0.020f, 0.4f, EIGToneWaveform::ValueNoise});
	TakeNotes.Add({0.0f, TakeEnd, 1500.0f, 0.015f, 0.030f, 0.4f, EIGToneWaveform::ValueNoise});

	for (const FIGRecordedSound& Sound : Sounds)
	{
		if (Sound.bSuppressed)
		{
			// The whole rule, in one skipped iteration. Its duration is already
			// in TakeEnd, so the timeline keeps the room exactly this long.
			continue;
		}
		const float Level = FMath::Clamp(Sound.Loudness, 0.0f, 1.0f);
		if (Sound.bBody)
		{
			// 그녀의 숨. 작은 스피커로 들어도 들이켜는 숨은 숨이다. 3연이 비운
			// 자리 한가운데 이것만 남는다 — 그의 노크는 없고, 놀란 그녀만 있다.
			TakeNotes.Add({Sound.OffsetSeconds, 0.34f, 1800.0f, 0.070f, 0.60f, 1.0f, EIGToneWaveform::BandNoise, 0.35f});
			continue;
		}
		if (Sound.bFromEntity)
		{
			// 규칙이 풀린 뒤의 그의 소리. 벽을 울린 저역은 폰 스피커를 못 지나고
			// 관절이 닿는 딱 소리와 판의 울림만 남는다. 기록 하나가 타격 하나다.
			TakeNotes.Add({Sound.OffsetSeconds, 0.045f, 1100.0f, 0.05f + 0.08f * Level, 0.004f, 2.6f, EIGToneWaveform::ValueNoise});
			TakeNotes.Add({Sound.OffsetSeconds, 0.030f, 2600.0f, 0.03f + 0.05f * Level, 0.003f, 3.0f, EIGToneWaveform::ValueNoise});
			TakeNotes.Add({Sound.OffsetSeconds, 0.110f, 520.0f, 0.03f + 0.04f * Level, 0.006f, 2.2f, EIGToneWaveform::Sine});
			continue;
		}
		// Her own body, through a phone speaker. Band-limited on purpose: the
		// low thump of a real footfall is not what a small speaker gives back.
		TakeNotes.Add({
			Sound.OffsetSeconds,
			0.055f,
			900.0f,
			0.045f + 0.075f * Level,
			0.010f,
			1.7f,
			EIGToneWaveform::ValueNoise});
		TakeNotes.Add({
			Sound.OffsetSeconds,
			0.038f,
			2400.0f,
			0.026f + 0.050f * Level,
			0.008f,
			2.0f,
			EIGToneWaveform::ValueNoise});
		// A trace of the body under it, still above the speaker's floor.
		TakeNotes.Add({
			Sound.OffsetSeconds,
			0.070f,
			430.0f,
			0.020f + 0.038f * Level,
			0.012f,
			2.4f,
			EIGToneWaveform::Sine});
	}

	Wave->ConfigureNotes(MoveTemp(TakeNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCrtChannelSwitch(
	UObject* Outer,
	const bool bCollapse)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		bCollapse ? TEXT("IGCrtChannelCollapse") : TEXT("IGCrtChannelAcquire"));
	TArray<FIGToneNote> SwitchNotes;

	// Korean mains is 60 Hz, so the hum under every tube in this building is 60
	// and its harmonics — not the 50 a European monitor would give.
	constexpr float MainsHz = 60.0f;
	// NTSC horizontal line rate. Mostly felt rather than heard, and that is the
	// point: it is the difference between a screen and a picture of a screen.
	constexpr float LineWhineHz = 15734.0f;

	if (!bCollapse)
	{
		// Acquire. Snow first and loudest, because the tube shows noise before it
		// shows anything; the hum rises underneath as the input takes hold.
		SwitchNotes.Add({0.000f, 0.350f, 9000.0f, 0.185f, 0.004f, 2.6f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.000f, 0.320f, 4200.0f, 0.120f, 0.004f, 2.4f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.000f, 0.260f, 1500.0f, 0.062f, 0.006f, 2.8f, EIGToneWaveform::ValueNoise});
		// Two horizontal tears while the sync separator hunts.
		SwitchNotes.Add({0.058f, 0.020f, 2600.0f, 0.090f, 0.002f, 3.4f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.148f, 0.016f, 3300.0f, 0.070f, 0.002f, 3.6f, EIGToneWaveform::ValueNoise});
		// The hum arrives and stays — it is handed over to the looping bed.
		SwitchNotes.Add({0.040f, 0.320f, MainsHz, 0.052f, 0.520f, 0.5f, EIGToneWaveform::Sine});
		SwitchNotes.Add({0.040f, 0.320f, MainsHz * 2.0f, 0.030f, 0.560f, 0.5f, EIGToneWaveform::Sine});
		SwitchNotes.Add({0.090f, 0.270f, LineWhineHz, 0.012f, 0.600f, 0.6f, EIGToneWaveform::Sine});
	}
	else
	{
		// Death. The picture is torn away rather than faded: the hiss swells into
		// the tear, and the hum is the last thing to go because the tube keeps its
		// charge for a moment after it loses the signal.
		SwitchNotes.Add({0.000f, 0.240f, 6200.0f, 0.090f, 0.340f, 1.2f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.180f, 0.520f, 9600.0f, 0.205f, 0.030f, 1.9f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.180f, 0.480f, 3800.0f, 0.135f, 0.030f, 1.8f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.180f, 0.400f, 1200.0f, 0.058f, 0.040f, 2.0f, EIGToneWaveform::ValueNoise});
		// Three tears, closer together each time: the channel is not coming back.
		SwitchNotes.Add({0.196f, 0.026f, 2200.0f, 0.115f, 0.002f, 3.2f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.352f, 0.022f, 2900.0f, 0.100f, 0.002f, 3.4f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.470f, 0.018f, 3600.0f, 0.082f, 0.002f, 3.6f, EIGToneWaveform::ValueNoise});
		SwitchNotes.Add({0.000f, 0.760f, MainsHz, 0.048f, 0.060f, 1.1f, EIGToneWaveform::Sine});
		SwitchNotes.Add({0.000f, 0.700f, MainsHz * 2.0f, 0.026f, 0.060f, 1.3f, EIGToneWaveform::Sine});
		SwitchNotes.Add({0.000f, 0.560f, LineWhineHz, 0.011f, 0.040f, 1.6f, EIGToneWaveform::Sine});
		// The four-way split settling back in: one relay-quiet thump of the
		// deflection yoke, and then the booth is as silent as it was.
		SwitchNotes.Add({0.690f, 0.130f, 210.0f, 0.040f, 0.020f, 2.6f, EIGToneWaveform::Sine});
	}

	Wave->ConfigureNotes(MoveTemp(SwitchNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCrtChannelBed(
	UObject* Outer,
	const float TotalSeconds)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGCrtChannelBed"));
	TArray<FIGToneNote> HumNotes;

	const float Seconds = FMath::Clamp(TotalSeconds, 1.0f, 30.0f);
	constexpr float MainsHz = 60.0f;
	constexpr float LineWhineHz = 15734.0f;

	// One long note per partial. The 0.25 release power is the synth's flattest
	// decay, so the bed holds near full for most of the window and only gives up
	// the last of itself at the end — the tube dimming as the input goes, not a
	// fade someone applied. Quiet enough that the §10.2 listening duck still owns
	// the room: this must never compete with what she is trying to hear.
	HumNotes.Add({0.000f, Seconds, MainsHz, 0.034f, 0.045f, 0.25f, EIGToneWaveform::Sine});
	HumNotes.Add({0.000f, Seconds, MainsHz * 2.0f, 0.021f, 0.050f, 0.25f, EIGToneWaveform::Sine});
	HumNotes.Add({0.000f, Seconds, MainsHz * 3.0f, 0.010f, 0.055f, 0.25f, EIGToneWaveform::Sine});
	HumNotes.Add({0.000f, Seconds, 7400.0f, 0.014f, 0.040f, 0.25f, EIGToneWaveform::ValueNoise});
	HumNotes.Add({0.000f, Seconds, LineWhineHz, 0.009f, 0.060f, 0.25f, EIGToneWaveform::Sine});

	Wave->ConfigureNotes(MoveTemp(HumNotes), false);
	// Analog sync was never stable. A third of a percent at 0.6 Hz is under the
	// threshold of hearing it as vibrato and over the threshold of hearing the
	// difference between this and a synthesised tone.
	Wave->ConfigurePitchWow(0.003f, 0.6f);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreatePipeWaterFlow(
	UObject* Outer,
	const int32 DistanceStep)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGPipeWaterFlow"));
	TArray<FIGToneNote> FlowNotes;

	// §21.3 원근 4단. Concrete and board are a low-pass filter, so the band the
	// water arrives in is the distance it travelled. Nothing else needs to say
	// how far away it is.
	const int32 Step = FMath::Clamp(DistanceStep, 0, 3);
	const float Bandwidths[] = {5000.0f, 2400.0f, 1100.0f, 480.0f};
	const float Levels[] = {0.085f, 0.062f, 0.042f, 0.026f};
	const float Bandwidth = Bandwidths[Step];
	const float Level = Levels[Step];
	constexpr float LoopSeconds = 1.60f;

	// Two overlapping halves so the loop seam never lands on a silence, and a
	// slow body under them: moving water is never a steady tone.
	FlowNotes.Add({0.000f, 0.96f, Bandwidth, Level, 0.180f, 0.9f, EIGToneWaveform::ValueNoise});
	FlowNotes.Add({0.780f, 0.96f, Bandwidth * 0.92f, Level * 0.94f, 0.200f, 0.9f, EIGToneWaveform::ValueNoise});
	FlowNotes.Add({0.000f, LoopSeconds, Bandwidth * 0.26f, Level * 0.55f, 0.250f, 0.8f, EIGToneWaveform::ValueNoise});

	// Ticks are the sound of water hitting the inside of a pipe, and only the
	// near steps keep them: through two walls the ticks are gone before the hum.
	if (Step <= 1)
	{
		const float TickStarts[] = {0.113f, 0.402f, 0.667f, 0.941f, 1.284f};
		for (int32 TickIndex = 0; TickIndex < UE_ARRAY_COUNT(TickStarts); ++TickIndex)
		{
			FlowNotes.Add({
				TickStarts[TickIndex],
				0.016f,
				Bandwidth * (0.70f + 0.06f * (TickIndex % 3)),
				Level * (Step == 0 ? 0.52f : 0.30f),
				0.010f,
				1.6f,
				EIGToneWaveform::ValueNoise});
		}
	}

	Wave->ConfigureNotes(MoveTemp(FlowNotes), true, LoopSeconds);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateWallCavityResponse(
	UObject* Outer,
	const bool bHollow)
{
	UIGToneSequenceSoundWave* Wave = IGToneSequence::NewWave(
		Outer,
		bHollow ? TEXT("IGWallCavityHollow") : TEXT("IGWallCavitySolid"));
	TArray<FIGToneNote> WallNotes;

	// The same excitation both times: an ear settling against board while the
	// riser drives it from behind. If this differed, the player would be reading
	// the contact instead of the wall.
	WallNotes.Add({0.000f, 0.030f, 2600.0f, 0.038f, 0.010f, 1.4f, EIGToneWaveform::ValueNoise});

	if (bHollow)
	{
		// 105 Hz is the mass-air-mass resonance of a real cavity stud wall — two
		// leaves rocking on the air between them. Low release powers keep it
		// ringing for a second and a half: 빈 벽은 길게 운다.
		WallNotes.Add({0.006f, 1.450f, 105.0f, 0.130f, 0.014f, 0.95f, EIGToneWaveform::Sine});
		// The cavity's own axial modes: about 2.4 m tall gives ~71 Hz, and the
		// shaft-side gap of roughly 40 cm gives ~430 Hz.
		WallNotes.Add({0.010f, 1.320f, 71.0f, 0.072f, 0.020f, 1.05f, EIGToneWaveform::Sine});
		WallNotes.Add({0.004f, 0.880f, 215.0f, 0.048f, 0.012f, 1.15f, EIGToneWaveform::Triangle});
		WallNotes.Add({0.008f, 0.620f, 430.0f, 0.030f, 0.014f, 1.30f, EIGToneWaveform::Sine});
		// Water heard through nothing but air, arriving with the ring.
		WallNotes.Add({0.020f, 1.180f, 4200.0f, 0.026f, 0.120f, 1.10f, EIGToneWaveform::ValueNoise});
	}
	else
	{
		// No air spring, no resonance. One damped board note and it is over in
		// under a third of a second: 속이 찬 벽은 짧게 죽는다.
		WallNotes.Add({0.004f, 0.220f, 150.0f, 0.110f, 0.016f, 3.00f, EIGToneWaveform::Sine});
		WallNotes.Add({0.004f, 0.140f, 320.0f, 0.042f, 0.014f, 3.40f, EIGToneWaveform::Triangle});
		// Whatever water reaches here came the long way round, through mass.
		WallNotes.Add({0.018f, 0.300f, 520.0f, 0.020f, 0.140f, 2.40f, EIGToneWaveform::ValueNoise});
	}

	Wave->ConfigureNotes(MoveTemp(WallNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateCordlessDriverRun(UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGCordlessDriverRun"));
	TArray<FIGToneNote> DriverNotes;

	// 다섯 번의 나사. 방아쇠 딸깍, 165Hz 모터에 배음과 2.4kHz 기어 대역 잡음,
	// 끝에서 부하로 128Hz까지 처지고, 놓으면 딸깍. 옥상 콘크리트 위 10.9초.
	// 다섯 번을 다 적었다 — 정적 헤드룸 감사가 읽을 수 있게.
	DriverNotes.Add({0.000f, 0.030f, 1600.0f, 0.040f, 0.005f, 2.0f, EIGToneWaveform::ValueNoise});
	DriverNotes.Add({0.000f, 1.400f, 165.0f, 0.048f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({0.000f, 1.400f, 330.0f, 0.020f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({0.000f, 1.400f, 2400.0f, 0.018f, 0.080f, 1.2f, EIGToneWaveform::BandNoise, 0.55f});
	DriverNotes.Add({0.980f, 0.490f, 128.0f, 0.036f, 0.200f, 2.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({1.400f, 0.040f, 1200.0f, 0.036f, 0.005f, 2.2f, EIGToneWaveform::ValueNoise});

	DriverNotes.Add({2.100f, 0.030f, 1600.0f, 0.040f, 0.005f, 2.0f, EIGToneWaveform::ValueNoise});
	DriverNotes.Add({2.100f, 1.100f, 165.0f, 0.048f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({2.100f, 1.100f, 330.0f, 0.020f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({2.100f, 1.100f, 2400.0f, 0.018f, 0.080f, 1.2f, EIGToneWaveform::BandNoise, 0.55f});
	DriverNotes.Add({2.870f, 0.390f, 128.0f, 0.036f, 0.200f, 2.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({3.200f, 0.040f, 1200.0f, 0.036f, 0.005f, 2.2f, EIGToneWaveform::ValueNoise});

	DriverNotes.Add({4.300f, 0.030f, 1600.0f, 0.040f, 0.005f, 2.0f, EIGToneWaveform::ValueNoise});
	DriverNotes.Add({4.300f, 1.700f, 165.0f, 0.048f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({4.300f, 1.700f, 330.0f, 0.020f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({4.300f, 1.700f, 2400.0f, 0.018f, 0.080f, 1.2f, EIGToneWaveform::BandNoise, 0.55f});
	DriverNotes.Add({5.490f, 0.600f, 128.0f, 0.036f, 0.200f, 2.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({6.000f, 0.040f, 1200.0f, 0.036f, 0.005f, 2.2f, EIGToneWaveform::ValueNoise});

	DriverNotes.Add({6.900f, 0.030f, 1600.0f, 0.040f, 0.005f, 2.0f, EIGToneWaveform::ValueNoise});
	DriverNotes.Add({6.900f, 0.900f, 165.0f, 0.048f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({6.900f, 0.900f, 330.0f, 0.020f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({6.900f, 0.900f, 2400.0f, 0.018f, 0.080f, 1.2f, EIGToneWaveform::BandNoise, 0.55f});
	DriverNotes.Add({7.530f, 0.320f, 128.0f, 0.036f, 0.200f, 2.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({7.800f, 0.040f, 1200.0f, 0.036f, 0.005f, 2.2f, EIGToneWaveform::ValueNoise});

	DriverNotes.Add({9.400f, 0.030f, 1600.0f, 0.040f, 0.005f, 2.0f, EIGToneWaveform::ValueNoise});
	DriverNotes.Add({9.400f, 1.500f, 165.0f, 0.048f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({9.400f, 1.500f, 330.0f, 0.020f, 0.060f, 1.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({9.400f, 1.500f, 2400.0f, 0.018f, 0.080f, 1.2f, EIGToneWaveform::BandNoise, 0.55f});
	DriverNotes.Add({10.450f, 0.530f, 128.0f, 0.036f, 0.200f, 2.4f, EIGToneWaveform::SoftSquare});
	DriverNotes.Add({10.900f, 0.040f, 1200.0f, 0.036f, 0.005f, 2.2f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(DriverNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateValveOpen(
	UObject* Outer,
	const int32 ValveIndex)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGValveOpen"));
	TArray<FIGToneNote> ValveNotes;

	// §21.3: 1.8 kHz metal ringing plus a 1.2 s ramp of water starting to move,
	// 2.00 s total. Bigger wheels ring lower and fill slower — the three
	// authored valves stay distinguishable by ear alone (§10.3 밸브 3종).
	const int32 Index = FMath::Clamp(ValveIndex, 0, 2);
	const float RingHz[] = {1800.0f, 1520.0f, 2150.0f};
	const float FillSeconds[] = {1.20f, 1.34f, 1.06f};
	const float Ring = RingHz[Index];
	const float Fill = FillSeconds[Index];

	// The stem breaking free, then the wheel turning in three dry clicks.
	ValveNotes.Add({0.000f, 0.048f, 1400.0f, 0.070f, 0.008f, 1.8f, EIGToneWaveform::ValueNoise});
	ValveNotes.Add({0.000f, 0.340f, Ring, 0.062f, 0.006f, 2.6f, EIGToneWaveform::Sine});
	ValveNotes.Add({0.000f, 0.520f, Ring * 0.5f, 0.034f, 0.010f, 2.2f, EIGToneWaveform::Triangle});
	const float ClickStarts[] = {0.185f, 0.372f, 0.548f};
	for (const float ClickStart : ClickStarts)
	{
		ValveNotes.Add({ClickStart, 0.026f, 1100.0f, 0.038f, 0.010f, 1.7f, EIGToneWaveform::ValueNoise});
		ValveNotes.Add({ClickStart, 0.150f, Ring * 0.92f, 0.026f, 0.008f, 2.8f, EIGToneWaveform::Sine});
	}

	// Water arriving: the band opens up over the fill as the pipe charges, so
	// the ramp is heard as pressure building rather than a fade-in.
	constexpr int32 RampSteps = 6;
	for (int32 RampIndex = 0; RampIndex < RampSteps; ++RampIndex)
	{
		const float Alpha = static_cast<float>(RampIndex) / (RampSteps - 1);
		const float Start = 0.62f + Fill * Alpha * 0.82f;
		ValveNotes.Add({
			Start,
			Fill * 0.42f,
			FMath::Lerp(620.0f, 3400.0f, Alpha),
			FMath::Lerp(0.020f, 0.058f, Alpha),
			0.220f,
			1.1f,
			EIGToneWaveform::ValueNoise});
	}
	// Settled flow holding the last of the two seconds.
	ValveNotes.Add({1.520f, 0.480f, 2600.0f, 0.044f, 0.180f, 1.2f, EIGToneWaveform::ValueNoise});
	ValveNotes.Add({1.520f, 0.480f, 700.0f, 0.026f, 0.200f, 1.1f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(ValveNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateHammerImpact(
	UObject* Outer,
	const int32 StrikeIndex)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGHammerImpact"));
	TArray<FIGToneNote> HammerNotes;

	// §21.3: a 90 Hz impulse, gypsum fracture, and 1.4 s of the building
	// answering, over 1.60 s. §10.3 wants the fracture in three stages, and the
	// stage is the information: the player hears the wall going, not a counter.
	const int32 Strike = FMath::Max(0, StrikeIndex);
	const int32 Stage = Strike <= 1 ? 0 : (Strike <= 3 ? 1 : 2);
	const float StageGain[] = {0.62f, 0.84f, 1.00f};
	const float Gain = StageGain[Stage];

	// The head landing. Low, and the same every time — the arm does not change.
	HammerNotes.Add({0.000f, 0.180f, 90.0f, 0.300f, 0.003f, 2.4f, EIGToneWaveform::Sine});
	HammerNotes.Add({0.000f, 0.090f, 240.0f, 0.120f, 0.004f, 2.8f, EIGToneWaveform::Triangle});
	HammerNotes.Add({0.000f, 0.022f, 3200.0f, 0.090f * Gain, 0.006f, 1.5f, EIGToneWaveform::ValueNoise});

	// Stage 0: the board bruises and holds. A dull crush, nothing separating.
	// Stage 1: paper tears and the core starts letting go.
	// Stage 2: it breaks through — pieces, and the cavity behind them.
	const int32 FractureCount = 3 + Stage * 3;
	for (int32 FractureIndex = 0; FractureIndex < FractureCount; ++FractureIndex)
	{
		const float Spread = static_cast<float>(FractureIndex) / FractureCount;
		HammerNotes.Add({
			0.026f + Spread * (0.140f + 0.120f * Stage),
			0.020f + 0.014f * (FractureIndex % 3),
			FMath::Lerp(1400.0f, 4600.0f, FMath::Frac(Spread * 2.7f)),
			(0.034f + 0.016f * Stage) * (1.0f - Spread * 0.45f),
			0.008f,
			1.4f,
			EIGToneWaveform::ValueNoise});
	}
	if (Stage == 2)
	{
		// The cavity is open now, so its mass-air-mass note rings free instead
		// of being muffled by the board that used to close it.
		HammerNotes.Add({0.060f, 0.900f, 105.0f, 0.090f, 0.014f, 1.10f, EIGToneWaveform::Sine});
	}

	// 1.4 s of building. Concrete keeps the high end, which is why a hammer at
	// 04:30 is the loudest mistake available (§5.1 소음 1.0).
	HammerNotes.Add({0.040f, 1.400f, 170.0f, 0.070f * Gain, 0.030f, 1.20f, EIGToneWaveform::ValueNoise});
	HammerNotes.Add({0.055f, 1.320f, 900.0f, 0.040f * Gain, 0.060f, 1.35f, EIGToneWaveform::ValueNoise});
	HammerNotes.Add({0.070f, 1.180f, 2400.0f, 0.024f * Gain, 0.090f, 1.50f, EIGToneWaveform::ValueNoise});
	// Dust coming off the break, arriving last.
	HammerNotes.Add({0.320f, 1.280f, 6800.0f, 0.016f * Gain, 0.140f, 2.60f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(HammerNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateHammerFractureLayer(
	UObject* Outer,
	const int32 StrikeIndex)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGHammerFractureLayer"));
	TArray<FIGToneNote> FractureNotes;
	const int32 Strike = FMath::Clamp(StrikeIndex, 0, 4);

	// 녹음 머리가 가장 센 첫 20ms는 비워 둔다. 파쇄는 그 뒤에 온다.
	if (Strike == 0)
	{
		// 1타. 종이 면이 갈라지고 판은 버틴다. 둔하게 짓눌리는 소리와 금 셋.
		FractureNotes.Add({0.018f, 0.070f, 900.0f, 0.080f, 0.020f, 1.8f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.026f, 0.020f, 2300.0f, 0.070f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.072f, 0.034f, 3500.0f, 0.058f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.124f, 0.048f, 1600.0f, 0.048f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
	}
	else if (Strike < 4)
	{
		// 2~4타. 종이가 찢기고 심이 떨어져 나간다. 그 뒤로 두 겹 판 사이의
		// 공기가 운다 — P3에서 귀로 배운 「빈 벽은 길게 운다」가 망치로 돌아온다.
		FractureNotes.Add({0.024f, 0.022f, 1900.0f, 0.090f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.056f, 0.034f, 3900.0f, 0.080f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.094f, 0.048f, 2600.0f, 0.074f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.138f, 0.022f, 4400.0f, 0.064f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.186f, 0.034f, 1500.0f, 0.054f, 0.008f, 1.4f, EIGToneWaveform::ValueNoise});
		// 종이가 길게 찢긴다.
		FractureNotes.Add({0.030f, 0.200f, 2800.0f, 0.048f, 0.100f, 1.6f, EIGToneWaveform::ValueNoise});
		// 105Hz 판-공기-판 공명과 2.4m 높이의 71Hz. 아직 판이 막고 있어 짧다.
		FractureNotes.Add({0.050f, 0.620f, 105.0f, 0.120f, 0.012f, 1.20f, EIGToneWaveform::Sine});
		FractureNotes.Add({0.056f, 0.520f, 71.0f, 0.060f, 0.018f, 1.30f, EIGToneWaveform::Sine});
	}
	else
	{
		// 5타. 뚫린다. 조각이 촘촘하게 떨어져 나간다.
		FractureNotes.Add({0.022f, 0.022f, 2100.0f, 0.074f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.046f, 0.030f, 4200.0f, 0.066f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.074f, 0.044f, 1500.0f, 0.062f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.104f, 0.022f, 3300.0f, 0.056f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.138f, 0.030f, 2500.0f, 0.052f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.176f, 0.044f, 4600.0f, 0.046f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.218f, 0.022f, 1800.0f, 0.042f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.262f, 0.030f, 3700.0f, 0.038f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.310f, 0.044f, 2200.0f, 0.034f, 0.006f, 1.4f, EIGToneWaveform::ValueNoise});
		// 판이 통째로 내려앉는다. §8 4-2가 말한 게임 최대 음압은 녹음의 머리와
		// 이 저역이 나눠 진다. 머리가 다 죽은 0.15초 뒤라 둘이 겹쳐 깎이지 않는다.
		FractureNotes.Add({0.150f, 0.360f, 52.0f, 0.260f, 0.012f, 2.2f, EIGToneWaveform::Sub});
		FractureNotes.Add({0.152f, 0.170f, 140.0f, 0.090f, 0.010f, 2.6f, EIGToneWaveform::Triangle});
		FractureNotes.Add({0.150f, 0.090f, 700.0f, 0.060f, 0.010f, 2.0f, EIGToneWaveform::ValueNoise});
		// 막고 있던 판이 없으니 공동이 길게 운다.
		FractureNotes.Add({0.060f, 0.900f, 105.0f, 0.090f, 0.014f, 1.10f, EIGToneWaveform::Sine});
		FractureNotes.Add({0.070f, 0.800f, 71.0f, 0.045f, 0.020f, 1.20f, EIGToneWaveform::Sine});
		// 건물이 받는다. 녹음에는 이 꼬리가 없다.
		FractureNotes.Add({0.080f, 1.300f, 160.0f, 0.045f, 0.040f, 1.30f, EIGToneWaveform::ValueNoise});
		// 파편이 공동 바닥과 복도에 떨어진다. 몇 개는 바닥을 친다.
		FractureNotes.Add({0.300f, 0.040f, 2400.0f, 0.050f, 0.010f, 1.8f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.302f, 0.050f, 230.0f, 0.040f, 0.010f, 2.8f, EIGToneWaveform::Sine});
		FractureNotes.Add({0.395f, 0.036f, 1300.0f, 0.042f, 0.010f, 1.8f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.470f, 0.044f, 2000.0f, 0.034f, 0.010f, 1.8f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.472f, 0.046f, 190.0f, 0.030f, 0.010f, 2.8f, EIGToneWaveform::Sine});
		FractureNotes.Add({0.560f, 0.030f, 2600.0f, 0.028f, 0.010f, 1.8f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.640f, 0.040f, 1100.0f, 0.022f, 0.010f, 1.8f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.730f, 0.036f, 1700.0f, 0.018f, 0.010f, 1.8f, EIGToneWaveform::ValueNoise});
		FractureNotes.Add({0.820f, 0.030f, 900.0f, 0.015f, 0.010f, 1.8f, EIGToneWaveform::ValueNoise});
		// 가루가 마지막에 내려앉는다.
		FractureNotes.Add({0.320f, 1.000f, 6800.0f, 0.016f, 0.140f, 2.60f, EIGToneWaveform::ValueNoise});
	}

	Wave->ConfigureNotes(MoveTemp(FractureNotes), false);
	return Wave;
}

UIGToneSequenceSoundWave* UIGToneSequenceSoundWave::CreateWallpaperSeamRoller(
	UObject* Outer)
{
	UIGToneSequenceSoundWave* Wave =
		IGToneSequence::NewWave(Outer, TEXT("IGWallpaperSeamRoller"));
	TArray<FIGToneNote> RollerNotes;

	// 이음 롤러는 페인트 롤러가 아니라 작은 고무 원통이다. 종이와 풀의
	// 넓은 마찰을 중심으로 두고, 방향이 바뀔 때만 작은 축이 반응한다.
	// 기계나 프린터처럼 규칙적으로 들리지 않도록 두 번의 길이를 다르게 둔다.
	RollerNotes.Add({0.000f, 1.48f, 1280.0f, 0.060f, 0.160f, 1.15f, EIGToneWaveform::ValueNoise});
	RollerNotes.Add({0.040f, 1.40f, 3600.0f, 0.025f, 0.220f, 1.30f, EIGToneWaveform::ValueNoise});
	RollerNotes.Add({0.080f, 1.26f, 185.0f, 0.022f, 0.180f, 1.20f, EIGToneWaveform::Triangle});
	RollerNotes.Add({1.510f, 0.055f, 780.0f, 0.048f, 0.006f, 2.6f, EIGToneWaveform::ValueNoise});
	RollerNotes.Add({1.510f, 0.120f, 142.0f, 0.034f, 0.008f, 2.8f, EIGToneWaveform::Sine});
	RollerNotes.Add({1.700f, 1.76f, 1120.0f, 0.057f, 0.180f, 1.10f, EIGToneWaveform::ValueNoise});
	RollerNotes.Add({1.760f, 1.66f, 3300.0f, 0.022f, 0.240f, 1.35f, EIGToneWaveform::ValueNoise});
	RollerNotes.Add({1.780f, 1.52f, 168.0f, 0.020f, 0.220f, 1.25f, EIGToneWaveform::Triangle});
	RollerNotes.Add({3.490f, 0.070f, 620.0f, 0.043f, 0.008f, 3.0f, EIGToneWaveform::ValueNoise});

	Wave->ConfigureNotes(MoveTemp(RollerNotes), false);
	return Wave;
}
