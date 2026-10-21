#include "Audio/IGAudioPresentationProbe.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGMissingFloorAudioSubsystem.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Audio/IGVoice.h"
#include "Accessibility/IGAccessibilitySubsystem.h"
#include "AudioMixerBlueprintLibrary.h"
#include "AudioDevice.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "Entity/IGListenerEntity.h"
#include "GameplayTagContainer.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Narrative/IGMissingFloorNarrativeSubsystem.h"
#include "Save/IGSaveSubsystem.h"
#include "Player/IGHorrorHUD.h"
#include "Player/IGPlayerCharacter.h"
#include "Player/IGStressComponent.h"
#include "Sound/SoundClass.h"

AIGAudioPresentationProbe::AIGAudioPresentationProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AIGAudioPresentationProbe::Check(const bool bCondition, const TCHAR* Name)
{
	Failures += bCondition ? 0 : 1;
	UE_LOG(LogTemp, Display, TEXT("AUDIO_PRESENTATION_CHECK %s %s"), Name,
		bCondition ? TEXT("PASS") : TEXT("FAIL"));
	// Shipping에서도 로그 설정과 관계없이 검사 결과를 남긴다.
	const FString Line = FString::Printf(TEXT("AUDIO_PRESENTATION_CHECK %s %s\n"),
		Name, bCondition ? TEXT("PASS") : TEXT("FAIL"));
	if (!FFileHelper::SaveStringToFile(Line,
		*(FPaths::ProjectSavedDir() / TEXT("AudioPresentation/receipt.txt")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append))
	{
		++Failures;
	}
}

void AIGAudioPresentationProbe::CheckVisibility()
{
	APlayerController* Controller = GetWorld()->GetFirstPlayerController();
	const FVector Eye = Player->GetPawnViewLocation();
	const FVector Face = Entity->GetCaptureFaceLocation();
	Controller->SetControlRotation((Face - Eye).Rotation());
	UBoxComponent* Wall = NewObject<UBoxComponent>(this);
	Wall->SetBoxExtent(FVector(8, 200, 300));
	Wall->SetCollisionProfileName(TEXT("BlockAll"));
	Wall->RegisterComponent();
	Wall->SetWorldLocation((Eye + Face) * 0.5f);
	Entity->TryCloseCallStinger(Player.Get(), 280);
	Check(Entity->LastCloseCallSeconds < 0, TEXT("wall_blocks_scare_without_spending_cooldown"));
	Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Controller->SetControlRotation(FRotator(85, 180, 0));
	Entity->TryCloseCallStinger(Player.Get(), 280);
	Check(Entity->LastCloseCallSeconds < 0, TEXT("looking_away_does_not_scare"));
	Controller->SetControlRotation((Face - Eye).Rotation());
	Audio->SetAuthoredSilence(true);
	Entity->TryCloseCallStinger(Player.Get(), 280);
	Check(Entity->LastCloseCallSeconds < 0, TEXT("silence_blocks_scare_without_spending_cooldown"));
	Audio->SetAuthoredSilence(false);
	Entity->TryCloseCallStinger(Player.Get(), 280);
	Check(Entity->LastCloseCallSeconds >= 0, TEXT("visible_face_triggers_scare"));
	const double FirstScare = Entity->LastCloseCallSeconds;
	Entity->TryCloseCallStinger(Player.Get(), 280);
	Check(Entity->LastCloseCallSeconds == FirstScare, TEXT("close_call_cooldown"));
	Wall->DestroyComponent();
}

void AIGAudioPresentationProbe::CheckMixerGain(
	const EIGAudioBus Bus, const float ExpectedDecibels, const TCHAR* Name)
{
	FAudioDevice* Device = GetWorld()->GetAudioDeviceRaw();
	if (!Device) { Check(false, Name); return; }
	USoundClass* SoundClass = Audio->GetBusSoundClass(Bus);
	float Actual = -1;
	// 보고용 계산식 대신 오디오 스레드가 실제 적용한 값을 읽는다.
	FAudioThread::RunCommandOnAudioThread([Device, SoundClass, &Actual]()
	{
		if (const FSoundClassProperties* Properties = Device->GetSoundClassCurrentProperties(SoundClass))
		{
			Actual = Properties->Volume;
		}
	});
	FAudioCommandFence Fence;
	Fence.BeginFence();
	Fence.Wait();
	const float Expected = ExpectedDecibels <= -96 ? 0 : FMath::Pow(10.f, ExpectedDecibels / 20.f);
	Check(FMath::IsNearlyEqual(Actual, Expected, 0.003f), Name);
	UE_LOG(LogTemp, Display, TEXT("AUDIO_PRESENTATION_MIX %s actual=%.5f expected=%.5f"), Name, Actual, Expected);
}

bool AIGAudioPresentationProbe::IsTrackedVoice(
	const UAudioComponent* Component, const EIGAudioBus Bus) const
{
	if (!Component || !Audio.IsValid())
	{
		return false;
	}
	for (const UIGMissingFloorAudioSubsystem::FTrackedVoice& Voice :
		Audio->ActiveVoices[static_cast<int32>(Bus)])
	{
		if (Voice.Component.Get() == Component)
		{
			return true;
		}
	}
	return false;
}

void AIGAudioPresentationProbe::CheckChaseWave()
{
	UIGToneSequenceSoundWave* Wave = UIGToneSequenceSoundWave::CreateChaseScore(this);
	const double Expected = 8.0 * 60.0 / 118.0;
	Check(FMath::Abs(Wave->LoopSampleCount / 48000.0 - Expected) < 2.0 / 48000.0,
		TEXT("chase_repeats_at_118_bpm_without_extra_gap"));
	TArray<uint8> Samples;
	int32 Peak = 0;
	int64 Energy = 0;
	// 세 마디를 실제 합성기로 굽는다. 설정값이 맞아도 무음이거나 잘린 파형이면 실패다.
	const int32 Count = static_cast<int32>(Wave->LoopSampleCount * 3);
	Wave->OnGeneratePCMAudio(Samples, Count);
	const int16* Pcm = reinterpret_cast<const int16*>(Samples.GetData());
	for (int32 Index = 0; Index < Count; ++Index)
	{
		Peak = FMath::Max(Peak, FMath::Abs(static_cast<int32>(Pcm[Index])));
		Energy += static_cast<int64>(Pcm[Index]) * Pcm[Index];
	}
	Check(Peak > 500 && Peak < 29000 && Energy > Count * 10000LL,
		TEXT("chase_wave_has_signal_and_headroom"));
	UE_LOG(LogTemp, Display, TEXT("AUDIO_PRESENTATION_WAVE seconds=%.6f peak=%.5f rms=%.5f"),
		Wave->LoopSampleCount / 48000.0, Peak / 32767.0,
		FMath::Sqrt(static_cast<double>(Energy) / Count) / 32767.0);
}

FText AIGAudioPresentationProbe::ProbeVoiceLine()
{
	// 키가 있는 문장이어야 목소리를 찾는다. 검사 문장이라 번역 목록에는 올리지 않는다.
	return FText::AsLocalizable_Advanced(TEXT("IGVoiceProbe"), TEXT("ProbeVoiceLine"), TEXT("목소리 검사 줄."));
}

void AIGAudioPresentationProbe::SayProbeVoice()
{
	FIGDialogueScope Scope;
	Scope.Context = TEXT("VoiceProbe");
	Scope.Owner = this;
	TWeakObjectPtr<AIGAudioPresentationProbe> WeakThis(this);
	Scope.OnEnded = [WeakThis](const EIGDialogueEndReason Reason)
	{
		if (AIGAudioPresentationProbe* Self = WeakThis.Get())
		{
			++Self->VoiceEnded;
			Self->VoiceLastReason = static_cast<uint8>(Reason);
		}
	};
	const FVector Mouth = Player.IsValid() ? Player->GetActorLocation() + FVector(160, 0, 60) : FVector::ZeroVector;
	IGVoice::Say(this, FText::FromString(TEXT("검사")), ProbeVoiceLine(), MoveTemp(Scope), [Mouth]() { return Mouth; });
}

void AIGAudioPresentationProbe::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Seconds += DeltaSeconds;
	if (Phase == 0)
	{
		// 창을 띄우지 않는 검사에서도 실제 믹서를 녹음한다.
		// Shipping에서는 실행 인자로 넘긴 ini 덮어쓰기가 적용되지 않을 수 있다.
		FApp::SetUnfocusedVolumeMultiplier(1.0f);
		FApp::SetVolumeMultiplier(1.0f);
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("AudioPresentation");
		IFileManager::Get().MakeDirectory(*Directory, true);
		FFileHelper::SaveStringToFile(TEXT(""), *(Directory / TEXT("receipt.txt")));
		APlayerController* Controller = GetWorld()->GetFirstPlayerController();
		Player = Controller ? Cast<AIGPlayerCharacter>(Controller->GetPawn()) : nullptr;
		if (!Player.IsValid())
		{
			if (Seconds > 15)
			{
				Check(false, TEXT("player_spawn"));
				FPlatformMisc::RequestExitWithStatus(false, 1);
			}
			return;
		}
		Audio = GetWorld()->GetSubsystem<UIGMissingFloorAudioSubsystem>();
		Stress = Player->GetStress();
		if (!Audio.IsValid() || !Stress.IsValid())
		{
			Check(false, TEXT("audio_and_stress_subsystems"));
			FPlatformMisc::RequestExitWithStatus(false, 1);
			return;
		}
		Audio->SetTitleMode(false);
		Audio->SetAuthoredSilence(false);
		Audio->SetUserMasterVolume(1);
		Audio->SetScoreUserVolume(1);
		Audio->SetAmbienceUserVolume(1);
		Audio->SetHeadphoneOutput(true);
		Player->GetCharacterMovement()->DisableMovement();
		Player->SetCameraMotionEnabled(false);
		Player->SetActorLocation(FVector(0, 0, 96));
		Entity = GetWorld()->SpawnActor<AIGListenerEntity>(FVector(280, 0, 58), FRotator(0, 180, 0));
		if (!Entity.IsValid())
		{
			Check(false, TEXT("entity_spawn"));
			FPlatformMisc::RequestExitWithStatus(false, 1);
			return;
		}
		Entity->SetActorTickEnabled(false);
		Entity->CachedPlayer = Player;
		Entity->UpdateDragLoop(160);
		Entity->UpdateBreathLoop(280);
		Check(Entity->DragLoopComponent && Entity->DragLoopComponent->IsPlaying(),
			TEXT("real_audio_device_and_drag_playback"));
		UAudioMixerBlueprintLibrary::StartRecordingOutput(this, 25);
		CheckChaseWave();
		Phase = 1; Seconds = 0;
	}
	else if (Phase == 1 && Seconds > 0.6f)
	{
		CheckMixerGain(EIGAudioBus::Entity, 0, TEXT("entity_bus_registered_unattenuated"));
		CheckMixerGain(EIGAudioBus::Player, -3, TEXT("player_bus_registered"));
		CheckMixerGain(EIGAudioBus::Puzzle, -1, TEXT("puzzle_bus_registered"));
		CheckMixerGain(EIGAudioBus::World, -8, TEXT("world_bus_registered"));
		CheckMixerGain(EIGAudioBus::UI, -10, TEXT("ui_bus_registered"));
		CheckMixerGain(EIGAudioBus::Score, -6, TEXT("score_bus_registered"));
		CheckVisibility();
		// 끌림과 숨은 상시 베드라 상한을 차지하지 않는다. 한 번 울고 마는 소리가 상한만큼
		// 들어와도 하나도 밀리지 않고, 뒤따라 쏟아지는 기는 걸음은 그 소리들을 밀지 못한다.
		// 세는 방식만 보는 검사라 작게 낸다. 같은 파형이 한 프레임에 겹치면 녹음이 깎인다.
		const int32 EntityCap = Audio->GetVoiceCap(EIGAudioBus::Entity);
		TArray<TWeakObjectPtr<UAudioComponent>> ScriptedCues;
		for (int32 Index = 0; Index < EntityCap; ++Index)
		{
			ScriptedCues.Add(IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateWallKnockSingle(this),
				Entity->GetActorLocation(), 0.05f, 1, 300, 1000, EIGAudioBus::Entity));
		}
		for (int32 Index = 0; Index < 8; ++Index)
		{
			IGAudio::SpawnExpendableOneShotAt(this, UIGToneSequenceSoundWave::CreateEntityCrawlStep(this, false),
				Entity->GetActorLocation(), 0.05f, 1, 200, 2200, EIGAudioBus::Entity);
		}
		int32 ScriptedAlive = 0;
		for (const TWeakObjectPtr<UAudioComponent>& Cue : ScriptedCues)
		{
			ScriptedAlive += IsTrackedVoice(Cue.Get(), EIGAudioBus::Entity) ? 1 : 0;
		}
		Check(ScriptedAlive == EntityCap, TEXT("beds_and_steps_leave_scripted_cues_alone"));
		// 두 상시 소리가 도는 중에 발음 상한을 넘긴다. 오래된 이동음이 멎으면 실패다.
		for (int32 Index = 0; Index < 8; ++Index)
		{
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateWallKnockSingle(this),
				Entity->GetActorLocation(), 0.4f, 1, 300, 1000, EIGAudioBus::Entity);
		}
		Audio->SetThreatState(EIGAudioThreatState::Chasing);
		Audio->SetEntityDistance(450);
		Chase = Audio->ScoreComponent;
		for (int32 Index = 1; Index <= 8; ++Index) { Audio->PlayTruthConfirmation(Index); }
		Phase = 2; Seconds = 0;
	}
	else if (Phase == 2 && Seconds > 1.0f)
	{
		// 확인음은 0.9초 뒤에 첫 타를 친다. 그 한 타가 추격 음악과 겹친 뒤에 본다.
		Check(Entity->DragLoopComponent->IsPlaying(), TEXT("drag_survives_voice_pressure"));
		Check(Entity->BreathLoopComponent->IsPlaying(), TEXT("entity_breath_survives_voice_pressure"));
		Check(Chase.IsValid() && Chase->IsPlaying(), TEXT("truth_cues_do_not_stop_chase_music"));
		Check(Audio->PlayStinger(EIGStinger::ChaseStart, Entity->GetActorLocation()), TEXT("first_chase_scream"));
		Check(!Audio->PlayStinger(EIGStinger::ChaseStart, Entity->GetActorLocation()), TEXT("repeat_chase_scream_suppressed"));
		Check(!Audio->PlayStinger(EIGStinger::CloseCall, Entity->GetActorLocation()), TEXT("close_call_does_not_stack_on_chase"));
		// 추격 진입 타격 뒤로 기는 걸음과 대본 원샷을 상한 넘게 쏟는다. 타격은 끝까지 간다.
		for (int32 Index = 0; Index < 6; ++Index)
		{
			IGAudio::SpawnExpendableOneShotAt(this, UIGToneSequenceSoundWave::CreateEntityCrawlStep(this, false),
				Entity->GetActorLocation(), 0.05f, 1, 200, 2200, EIGAudioBus::Entity);
			IGAudio::SpawnOneShotAt(this, UIGToneSequenceSoundWave::CreateWallKnockSingle(this),
				Entity->GetActorLocation(), 0.05f, 1, 300, 1000, EIGAudioBus::Entity);
		}
		Audio->SetThreatState(EIGAudioThreatState::Investigating);
		Phase = 3; Seconds = 0;
	}
	else if (Phase == 3 && Seconds > 0.7f)
	{
		Check(Chase.IsValid() && Chase->IsPlaying(), TEXT("chase_release_tail_survives"));
		// 펄스는 내려가 있고 그 자리에 클러스터 스탭 하나가 운다(§10.2).
		Check(IsValid(Audio->ChaseTailComponent) && Audio->ChaseTailComponent->IsPlaying()
			&& Audio->ChaseReleaseStopAtSeconds > 0.0, TEXT("chase_release_leaves_one_stab"));
		Check(IsValid(Audio->StingerComponent) && Audio->StingerComponent->IsPlaying(),
			TEXT("chase_stinger_survives_voice_pressure"));
		Audio->SetThreatState(EIGAudioThreatState::Chasing);
		Check(Audio->ScoreComponent == Chase.Get(), TEXT("chase_resumes_same_component_and_beat"));
		Check(!Audio->ChaseTailComponent && Audio->ChaseReleaseStopAtSeconds < 0.0,
			TEXT("chase_resume_clears_stab_and_stop"));
		Audio->SetEntityDistance(450);
		Phase = 4; Seconds = 0;
	}
	else if (Phase == 4 && Seconds > 0.6f)
	{
		Entity->BeginCapture(Player.Get());
		Stress->UpdateBreathLayer(0);
		Check(Audio->GetThreatState() == EIGAudioThreatState::Captured, TEXT("capture_has_own_presentation_state"));
		Check(!Audio->ScoreComponent && Audio->GetPresenceAlpha() == 0, TEXT("capture_clears_music_and_pressure"));
		Check(Stress->BreathLevelTarget == 0, TEXT("capture_leaves_room_for_contact_breath"));
		Phase = 5; Seconds = 0;
	}
	else if (Phase == 5 && Seconds > 0.5f)
	{
		Check(!Chase.IsValid() || !Chase->IsPlaying(), TEXT("chase_is_gone_during_capture"));
		Entity->SetDormant(true);
		Audio->SetThreatState(EIGAudioThreatState::Calm);
		Stress->SetExertion(1);
		Stress->UpdateBreathLayer(0);
		Stress->PlayGasp(true);
		InterruptedBreath = Stress->BreathOneShotComponent;
		Check(InterruptedBreath.IsValid(), TEXT("gasp_starts"));
		Stress->BeginBreathHold(4);
		Phase = 6; Seconds = 0;
	}
	else if (Phase == 6 && Seconds > 0.4f)
	{
		Check(!InterruptedBreath.IsValid() || !InterruptedBreath->IsPlaying(), TEXT("breath_hold_stops_existing_gasp"));
		Check(!Stress->BreathComponent || !Stress->BreathComponent->IsPlaying(), TEXT("breath_hold_stops_loop"));
		Stress->EndBreathHold(true);
		Stress->UpdateBreathLayer(0);
		Check(Stress->BreathOneShotComponent && Stress->BreathOneShotComponent->IsPlaying(), TEXT("release_has_one_gasp"));
		Audio->SetAuthoredSilence(true);
		Stress->UpdateBreathLayer(0);
		// P4처럼 침묵 한가운데서 진실이 확정된다. 타건을 붙잡는 것이 침묵뿐이게 해 둔다.
		Audio->PendingTruthStrikes.Reset();
		Audio->PlayTruthConfirmation(9);
		Audio->NextTruthStrikeWorldSeconds = 0.0;
		Phase = 7; Seconds = 0;
	}
	else if (Phase == 7 && Seconds > 0.4f)
	{
		Check(Audio->PendingTruthStrikes.Num() == 1, TEXT("truth_strike_waits_out_authored_silence"));
		CheckMixerGain(EIGAudioBus::Score, -96, TEXT("authored_silence_reaches_audio_device"));
		CheckMixerGain(EIGAudioBus::World, -24, TEXT("authored_world_duck_reaches_audio_device"));
		Check(!Stress->BreathComponent || !Stress->BreathComponent->IsPlaying(), TEXT("authored_silence_stops_breath_loop"));
		Stress->PlayGasp(true);
		Stress->PlayReliefExhale();
		Check(!Stress->BreathOneShotComponent, TEXT("authored_silence_blocks_breath_accents"));
		Audio->SetAuthoredSilence(false);
		Stress->SetExertion(1);
		Stress->UpdateBreathLayer(0);
		Check(Stress->BreathComponent && Stress->BreathComponent->IsPlaying(), TEXT("breath_returns_after_silence"));
		Audio->SetThreatState(EIGAudioThreatState::Chasing);
		Audio->SetEntityDistance(350);
		Phase = 8; Seconds = 0;
	}
	else if (Phase == 8 && Seconds > 4.5f)
	{
		// 거리 보고가 끊긴 뒤 잔류 압박이 사라지는지도 실제 틱으로 확인한다.
		Check(Audio->GetPresenceAlpha() == 0, TEXT("stale_entity_pressure_expires"));
		Check(Audio->PendingTruthStrikes.Num() == 0, TEXT("truth_strike_follows_released_silence"));
		Audio->SetTitleMode(true);
		Check(Audio->GetPresenceAlpha() == 0, TEXT("title_has_no_gameplay_pressure"));
		// 밤 5가 타이틀 위에 오르면 타이틀의 조율과 노크가 비킨다. 끝나면 처음처럼 돌아온다.
		Audio->HoldTitleSoundscape(true);
		Check(!Audio->ScoreComponent && Audio->NextTitleKnockRealTime < 0,
			TEXT("night_five_holds_title_soundscape"));
		Audio->HoldTitleSoundscape(false);
		Check(Audio->ScoreComponent && Audio->NextTitleKnockRealTime > 0,
			TEXT("title_soundscape_returns_after_night_five"));
		Stress->UpdateBreathLayer(0);
		Check(Stress->BreathLevelTarget == 0, TEXT("title_has_no_player_breath"));
		const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AudioPresentation"));
		IFileManager::Get().MakeDirectory(*Directory, true);
		UAudioMixerBlueprintLibrary::StopRecordingOutput(
			this, EAudioRecordingExportType::WavFile, TEXT("presentation-mix"), Directory);
		Phase = 9; Seconds = 0;
	}
	else if (Phase == 9 && Seconds > 2)
	{
		const FString Recording = FPaths::ProjectSavedDir() / TEXT("AudioPresentation/presentation-mix.wav");
		Check(IFileManager::Get().FileSize(*Recording) > 48000, TEXT("master_mix_recording_written"));
		Audio->SetScoreUserVolume(0);
		Phase = 10;
		Seconds = 0;
	}
	else if (Phase == 10 && Seconds > 0.4f)
	{
		CheckMixerGain(EIGAudioBus::Score, -96, TEXT("music_slider_mutes_real_bus"));
		CheckMixerGain(EIGAudioBus::Entity, 0, TEXT("music_slider_preserves_entity_cues"));
		Audio->SetScoreUserVolume(1);
		Audio->SetUserMasterVolume(0.5f);
		Phase = 11; Seconds = 0;
	}
	else if (Phase == 11 && Seconds > 0.4f)
	{
		CheckMixerGain(EIGAudioBus::Entity, -6.0206f, TEXT("master_slider_reaches_entity_bus"));
		CheckMixerGain(EIGAudioBus::Score, -12.0206f, TEXT("master_slider_reaches_score_bus"));
		Audio->SetTitleMode(false);
		Audio->SetThreatState(EIGAudioThreatState::Calm);
		Audio->SetEntityDistance(450);
		Audio->SetEntityDistance(1350);
		Audio->SetEntityDistance(3000);
		Check(Audio->GetPresenceAlpha() == 0, TEXT("nearly_silent_pressure_reaches_exact_zero"));
		Phase = 12; Seconds = 0;
	}
	else if (Phase == 12 && Seconds > 0.5f)
	{
		Check(!Audio->PresenceComponent || !Audio->PresenceComponent->IsPlaying(),
			TEXT("nearly_silent_pressure_loop_stops"));
		// 목소리 줄. 녹음이 아직 없으니 2.6초짜리 녹음 샘플을 검사용 대사 키에 건다.
		Audio->SetUserMasterVolume(1);
		APlayerController* Controller = GetWorld()->GetFirstPlayerController();
		Hud = Controller ? Cast<AIGHorrorHUD>(Controller->GetHUD()) : nullptr;
		USoundBase* StandIn = IGAudio::Sample(TEXT("Entity_KnockTriple"));
		Check(Hud.IsValid() && StandIn, TEXT("voice_probe_setup"));
		if (!Hud.IsValid() || !StandIn)
		{
			Phase = 30; Seconds = 0;
			return;
		}
		IGVoice::SetSoundForTesting(IGVoice::LineIdOf(ProbeVoiceLine()), StandIn);
		Hud->ClearDialogueForTesting();
		// 앞줄이 떠 있는 동안 녹음 줄은 줄만 서고 소리를 내지 않는다.
		AIGHorrorHUD::PushDialogue(this, FText::FromString(TEXT("검사")), FText::FromString(TEXT("목소리 앞의 글 줄.")));
		SayProbeVoice();
		Check(!Hud->GetActiveDialogueVoiceForTesting() && Hud->GetQueuedDialogueCountForTesting() == 2,
			TEXT("queued_voice_waits_for_its_turn"));
		Phase = 20; Seconds = 0;
	}
	else if (Phase == 20 && Hud.IsValid() && Hud->GetActiveDialogueVoiceForTesting())
	{
		VoiceComponent = Hud->GetActiveDialogueVoiceForTesting();
		Check(VoiceComponent->IsPlaying() && Hud->GetActiveDialogueLineForTesting().ToString() == ProbeVoiceLine().ToString(),
			TEXT("voice_starts_when_its_line_shows"));
		Phase = 21; Seconds = 0;
	}
	else if (Phase == 20 && Seconds > 8)
	{
		Check(false, TEXT("voice_starts_when_its_line_shows"));
		Phase = 30; Seconds = 0;
	}
	else if (Phase == 21 && Seconds > 1.0f)
	{
		// 사람이 말하는 동안 내 숨은 한 칸, 음악은 한 칸 내려간다(§21.1).
		Check(Audio->IsSpeechPlaying(), TEXT("speech_bus_reports_voice"));
		CheckMixerGain(EIGAudioBus::Player, -7, TEXT("speech_ducks_player_breath"));
		CheckMixerGain(EIGAudioBus::Speech, -2, TEXT("speech_bus_registered"));
		Phase = 22; Seconds = 0;
	}
	else if (Phase == 22 && (VoiceEnded > 0 || Seconds > 6))
	{
		const UIGMissingFloorNarrativeSubsystem* Narrative = GetGameInstance()
			? GetGameInstance()->GetSubsystem<UIGMissingFloorNarrativeSubsystem>() : nullptr;
		Check(VoiceEnded == 1 && VoiceLastReason == static_cast<uint8>(EIGDialogueEndReason::Completed)
			&& !(VoiceComponent.IsValid() && VoiceComponent->IsPlaying()), TEXT("voice_line_ends_with_its_sound"));
		Check(Narrative && Narrative->GetHeardLines().Num() > 0
			&& Narrative->GetHeardLines().Last().Line.EqualTo(ProbeVoiceLine()), TEXT("finished_voice_is_kept_as_heard"));
		// 음성 대사 자막을 끄면 소리만 난다. 줄은 그대로 서서 끝을 알린다.
		// 저장된 설정은 건드리지 않는다(검사가 끊겨도 사용자 설정이 꺼진 채 남지 않게).
		if (UIGAccessibilitySubsystem* Accessibility = GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>())
		{
			Accessibility->OverrideSubtitlesForTesting(false);
		}
		VoiceEnded = 0;
		SayProbeVoice();
		Phase = 23; Seconds = 0;
	}
	else if (Phase == 23 && Seconds > 0.6f)
	{
		const UAudioComponent* Voice = Hud.IsValid() ? Hud->GetActiveDialogueVoiceForTesting() : nullptr;
		Check(Voice && Voice->IsPlaying() && !Hud->IsActiveDialogueDrawnForTesting(), TEXT("voice_plays_with_subtitles_off"));
		Phase = 24; Seconds = 0;
	}
	else if (Phase == 24 && (VoiceEnded > 0 || Seconds > 6))
	{
		Check(VoiceEnded == 1 && VoiceLastReason == static_cast<uint8>(EIGDialogueEndReason::Completed),
			TEXT("hidden_voice_line_still_ends"));
		if (UIGAccessibilitySubsystem* Accessibility = GetGameInstance()->GetSubsystem<UIGAccessibilitySubsystem>())
		{
			Accessibility->OverrideSubtitlesForTesting(true);
		}
		VoiceEnded = 0;
		SayProbeVoice();
		Phase = 25; Seconds = 0;
	}
	else if (Phase == 25 && Seconds > 0.6f)
	{
		// 장면이 거두면 소리도 같이 멎는다.
		VoiceComponent = Hud.IsValid() ? Hud->GetActiveDialogueVoiceForTesting() : nullptr;
		const bool bWasPlaying = VoiceComponent.IsValid() && VoiceComponent->IsPlaying();
		AIGHorrorHUD::CancelDialogue(this, TEXT("VoiceProbe"));
		Check(bWasPlaying && !(VoiceComponent.IsValid() && VoiceComponent->IsPlaying())
			&& VoiceEnded == 1 && VoiceLastReason == static_cast<uint8>(EIGDialogueEndReason::Cancelled),
			TEXT("cancel_stops_the_voice"));
		VoiceEnded = 0;
		SayProbeVoice();
		Phase = 26; Seconds = 0;
	}
	else if (Phase == 26 && Seconds > 0.6f)
	{
		// 소리가 밖에서 먼저 멎으면 끝까지 들은 줄로 치지 않는다. 다음 일도 이어지지 않는다.
		if (UAudioComponent* Voice = Hud.IsValid() ? Hud->GetActiveDialogueVoiceForTesting() : nullptr)
		{
			Voice->Stop();
		}
		Phase = 27; Seconds = 0;
	}
	else if (Phase == 27 && Seconds > 0.3f)
	{
		Check(VoiceEnded == 1 && VoiceLastReason == static_cast<uint8>(EIGDialogueEndReason::Cancelled),
			TEXT("cut_voice_is_not_counted_as_finished"));
		IGVoice::SetSoundForTesting(IGVoice::LineIdOf(ProbeVoiceLine()), nullptr);
		// 「들은 말」이 든 채로 자동 저장을 쓴다. 이어하기와 같은 검사로 다시 읽혀야 한다. 저장 검사가
		// FText를 몰라서 대사를 들은 뒤의 저장이 모두 걸러지던 일(2026-10-21)을 배포본에서 다시 본다.
		if (UIGSaveSubsystem* Save = GetGameInstance() ? GetGameInstance()->GetSubsystem<UIGSaveSubsystem>() : nullptr)
		{
			Check(Save->RequestAutosave(
				FGameplayTag::RequestGameplayTag(FName(TEXT("Chapter.MissingFloor")), false),
				GetWorld()->GetOutermost()->GetFName(),
				FGameplayTag::RequestGameplayTag(FName(TEXT("Checkpoint.MissingFloor.Night")), false)),
				TEXT("heard_line_autosave_requested"));
		}
		Phase = 28; Seconds = 0;
	}
	else if (Phase == 28 && Seconds > 3.0f)
	{
		const UIGSaveSubsystem* Save = GetGameInstance() ? GetGameInstance()->GetSubsystem<UIGSaveSubsystem>() : nullptr;
		const int32 Heard = Save ? Save->GetLastAutosaveHeardLinesForTesting() : -1;
		UE_LOG(LogTemp, Display, TEXT("AUDIO_PRESENTATION_SAVE map=%s heard=%d"),
			*GetWorld()->GetOutermost()->GetName(), Heard);
		Check(Heard > 0, TEXT("autosave_with_heard_lines_is_loadable"));
		Phase = 30; Seconds = 0;
	}
	else if (Phase == 30)
	{
		UE_LOG(LogTemp, Display, TEXT("AUDIO_PRESENTATION_PROBE %s failures=%d"),
			Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
		const FString Summary = FString::Printf(TEXT("AUDIO_PRESENTATION_PROBE %s failures=%d\n"),
			Failures ? TEXT("FAIL") : TEXT("PASS"), Failures);
		if (!FFileHelper::SaveStringToFile(Summary,
			*(FPaths::ProjectSavedDir() / TEXT("AudioPresentation/receipt.txt")),
			FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_Append)) ++Failures;
		Phase = 13;
		FPlatformMisc::RequestExitWithStatus(false, Failures ? 1 : 0);
	}
}
