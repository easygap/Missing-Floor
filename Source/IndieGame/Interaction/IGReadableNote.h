#pragma once

#include "CoreMinimal.h"
#include "Interaction/IGInteractableActor.h"
#include "IGReadableNote.generated.h"

class AIGReadableNote;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FIGNoteReadSignature,
	AIGReadableNote*, Note,
	bool, bOpened);

/** 폰 화면에서 넘겨 보는 글 한 장. 앱 이름 아래 줄, 제목, 본문(마지막 줄은 글쓴이·댓글 수 같은 상태 줄). */
struct FIGPhonePage
{
	FText Subtitle;
	FText Title;
	TArray<FText> BodyLines;
};

/**
 * A piece of paper the player can stop and read: a building-management
 * notice taped to the lift doors, a memo on the fridge, a torn ledger page.
 *
 * Notes carry the story. Nothing in this game explains itself out loud, so
 * everything the player is allowed to learn is either seen or read.
 *
 * Reading opens a full-screen panel drawn by the HUD; the world keeps
 * running behind it, which is the point — you are standing in a dark
 * corridor with your face in a piece of paper.
 */
UCLASS(Blueprintable)
class INDIEGAME_API AIGReadableNote : public AIGInteractableActor
{
	GENERATED_BODY()

public:
	AIGReadableNote();

	/** Builds the physical reading prop; bound ledgers may opt into contact shadow. */
	void ConfigurePrototypeVisuals(
		UStaticMesh* CubeMesh,
		UMaterialInterface* PaperMaterial,
		const FVector& PaperSize,
		bool bCastPresentationShadow = false);

	/** 문단 사이의 빈 줄은 보존하고, 화면 폭에 따른 줄바꿈은 읽기 화면이 맡는다. */
	void SetNoteText(const FText& InTitle, TArray<FText> InBodyLines);

	/** Uses a dark smartphone notification screen instead of a paper sheet. */
	void SetPhoneNotificationPresentation();

	/** 폰 화면에서 첫 장(이 쪽지의 제목과 본문) 뒤로 넘겨 볼 글. 바뀌면 읽기 화면을 다시 짠다. */
	void SetExtraPhonePages(TArray<FIGPhonePage> InPages) { ExtraPhonePages = MoveTemp(InPages); ++PresentationRevision; }
	const TArray<FIGPhonePage>& GetExtraPhonePages() const { return ExtraPhonePages; }

	/** 현장에 놓인 인쇄 원본을 먼저 보여 주고, 다음 장에서 본문을 읽는다. */
	void SetReadingArtwork(UTexture2D* Texture) { ReadingArtwork = Texture; ++PresentationRevision; }
	UTexture2D* GetReadingArtwork() const { return ReadingArtwork; }
	uint32 GetPresentationRevision() const { return PresentationRevision; }

	/** Sets the prompt shown before it has been read (e.g. "공지 읽기"). */
	void SetInteractionPrompt(const FText& InPrompt) { OpenPrompt = InPrompt; }

	UFUNCTION(BlueprintPure, Category = "Note")
	bool IsOpen() const { return bOpen; }

	UFUNCTION(BlueprintPure, Category = "Note")
	const FText& GetTitle() const { return NoteTitle; }

	UFUNCTION(BlueprintPure, Category = "Note")
	const TArray<FText>& GetBodyLines() const { return NoteBodyLines; }

	UFUNCTION(BlueprintPure, Category = "Note|Phone")
	bool UsesPhoneNotificationPresentation() const
	{
		return bUsesPhoneNotificationPresentation;
	}

	/** Closes the panel; the HUD calls this when the player dismisses it. */
	UFUNCTION(BlueprintCallable, Category = "Note")
	void Close();

	/**
	 * 손에 든 종이의 소리. 펼 때는 그도 듣는 소리이고, 내려놓을 때는 더 작고
	 * 짧다. 플레이어가 직접 펴고 덮는 자리에서만 부른다 — Close()는 암전과 장면
	 * 전환에서도 강제로 불리므로 그 안에 넣으면 컷 밑에서 종이가 운다.
	 */
	void PlayHandlingSound(bool bOpening, float VolumeScale = 1.0f) const;

	/** 읽는 중에 쪽이 실제로 넘어갔다. 펼 때처럼 종이가 울고 그도 듣는다(§5.1). */
	void NotifyPageTurned(AActor* Reader) const;

	/**
	 * The note currently being read, if any. Only one can be open at a time,
	 * so a single weak pointer is enough for the HUD to find it without the
	 * HUD and the note having to know about each other.
	 */
	static AIGReadableNote* GetOpenNote();

	/** Broadcast when the panel opens (true) or closes (false). */
	UPROPERTY(BlueprintAssignable, Category = "Note|Events")
	FIGNoteReadSignature OnReadStateChanged;

	virtual FText GetInteractionPrompt_Implementation(AActor* Interactor) const override;
	virtual void CompleteInteraction_Implementation(const FIGInteractionContext& Context) override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Note|Components")
	TObjectPtr<UStaticMeshComponent> PaperMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note")
	FText NoteTitle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note")
	TArray<FText> NoteBodyLines;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note")
	FText OpenPrompt;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Note|Phone")
	bool bUsesPhoneNotificationPresentation = false;

private:
	UPROPERTY()
	TObjectPtr<UTexture2D> ReadingArtwork;
	uint32 PresentationRevision = 0;
	TArray<FIGPhonePage> ExtraPhonePages;
	static TWeakObjectPtr<AIGReadableNote> OpenNote;

	bool bOpen = false;
	bool bEverRead = false;
};
