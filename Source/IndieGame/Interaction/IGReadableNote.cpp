#include "Interaction/IGReadableNote.h"

#include "Audio/IGAudioHelpers.h"
#include "Audio/IGToneSequenceSoundWave.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Entity/IGNoiseSubsystem.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"

namespace IGReadableNote
{
	// §5.1: 종이 한 장. 게임에서 가장 조용한 조작이다.
	constexpr float PageLoudness = 0.08f;
}

TWeakObjectPtr<AIGReadableNote> AIGReadableNote::OpenNote;

AIGReadableNote* AIGReadableNote::GetOpenNote()
{
	return OpenNote.Get();
}

AIGReadableNote::AIGReadableNote()
{
	PrimaryActorTick.bCanEverTick = false;

	PaperMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Paper"));
	SetRootComponent(PaperMesh);
	PaperMesh->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	PaperMesh->SetGenerateOverlapEvents(false);
	PaperMesh->SetCanEverAffectNavigation(false);

	OpenPrompt = NSLOCTEXT("IGReadableNote", "DefaultPrompt", "읽기");
}

void AIGReadableNote::ConfigurePrototypeVisuals(
	UStaticMesh* CubeMesh,
	UMaterialInterface* PaperMaterial,
	const FVector& PaperSize,
	const bool bCastPresentationShadow)
{
	if (!CubeMesh)
	{
		return;
	}

	PaperMesh->SetStaticMesh(CubeMesh);
	PaperMesh->SetMaterial(0, PaperMaterial);
	PaperMesh->SetRelativeScale3D(PaperSize / 100.0f);
	// Loose wall paper stays shadowless to avoid acne; bound desk objects are
	// thick enough that their contact shadow is the cue preventing them from
	// reading as a card floating above the furniture.
	PaperMesh->SetCastShadow(bCastPresentationShadow);
}

void AIGReadableNote::HidePresentation()
{
	if (PaperMesh)
	{
		PaperMesh->SetVisibility(false, true);
		PaperMesh->SetCastShadow(false);
	}
}

void AIGReadableNote::SetNoteText(const FText& InTitle, TArray<FText> InBodyLines)
{
	NoteTitle = InTitle;
	NoteBodyLines = MoveTemp(InBodyLines);
	++PresentationRevision;
}

void AIGReadableNote::SetPhoneNotificationPresentation()
{
	bUsesPhoneNotificationPresentation = true;
}

FText AIGReadableNote::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	return OpenPrompt;
}

void AIGReadableNote::CompleteInteraction_Implementation(const FIGInteractionContext& Context)
{
	Super::CompleteInteraction_Implementation(Context);

	if (bOpen)
	{
		PlayHandlingSound(false);
		Close();
		return;
	}

	// Opening a second note closes the first: the panel is singular.
	if (AIGReadableNote* Previous = OpenNote.Get())
	{
		if (Previous != this)
		{
			Previous->Close();
		}
	}

	bOpen = true;
	++PresentationRevision;
	bEverRead = true;
	OpenNote = this;
	OnReadStateChanged.Broadcast(this, true);

	// Paper handled in a silent stairwell is barely a sound, but it is one.
	// Reported on open only; closing the panel moves nothing.
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(GetActorLocation(), IGReadableNote::PageLoudness, Context.Interactor);
		}
	}
	// 그가 듣는 그 종이 소리를 플레이어도 같은 자리에서 듣는다.
	PlayHandlingSound(true);
}

void AIGReadableNote::PlayHandlingSound(const bool bOpening, const float VolumeScale) const
{
	AIGReadableNote* Self = const_cast<AIGReadableNote*>(this);
	USoundBase* Sound = nullptr;
	float Volume = 0.0f;
	float Pitch = 1.0f;
	if (bUsesPhoneNotificationPresentation)
	{
		// 휴대전화는 종이가 아니다. 화면을 손톱으로 한 번 톡.
		Sound = UIGToneSequenceSoundWave::CreateMenuTick(Self, false);
		Volume = bOpening ? 0.20f : 0.14f;
		Pitch = 1.3f;
	}
	else
	{
		const auto PaperSynth = [Self]() -> USoundBase*
		{
			return UIGToneSequenceSoundWave::CreateJournalPageTurn(Self);
		};
		if (bOpening)
		{
			// §5.1에서 걸음(0.15)의 절반쯤 되는 조작이라 발소리보다 작게 둔다.
			Sound = IGAudio::SampleVariantOr(
				TEXT("Paper_Turn"), 2, static_cast<uint32>(FMath::Rand()), PaperSynth);
			Volume = 0.36f;
			Pitch = FMath::FRandRange(0.95f, 1.05f);
		}
		else
		{
			// 내려놓는 쪽은 짧은 장으로, 더 작고 조금 높게.
			Sound = IGAudio::SampleOr(TEXT("Paper_Turn_0"), PaperSynth);
			Volume = 0.22f;
			Pitch = 1.10f;
		}
	}
	IGAudio::SpawnOneShotAt(
		this,
		Sound,
		GetActorLocation(),
		Volume * VolumeScale,
		Pitch,
		60.0f,
		500.0f,
		EIGAudioBus::Player);
}

void AIGReadableNote::NotifyPageTurned(AActor* Reader) const
{
	if (!bOpen)
	{
		return;
	}
	// §5.1의 「쪽지 넘기기」. 펼 때와 같은 값이다 — 밤에 여러 장을 넘기면 그만큼
	// 여러 번 들린다.
	PlayHandlingSound(true, 0.7f);
	if (UWorld* World = GetWorld())
	{
		if (UIGNoiseSubsystem* Noise = World->GetSubsystem<UIGNoiseSubsystem>())
		{
			Noise->ReportNoise(GetActorLocation(), IGReadableNote::PageLoudness, Reader);
		}
	}
}

void AIGReadableNote::Close()
{
	if (!bOpen)
	{
		return;
	}

	bOpen = false;
	if (OpenNote.Get() == this)
	{
		OpenNote = nullptr;
	}
	OnReadStateChanged.Broadcast(this, false);
}
