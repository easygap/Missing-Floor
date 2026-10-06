#include "Player/IGCameraFeelModifier.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Player/IGPlayerCharacter.h"

bool UIGCameraFeelModifier::ModifyCamera(const float DeltaTime, FMinimalViewInfo& InOutPOV)
{
	const APlayerController* Controller =
		CameraOwner ? CameraOwner->GetOwningPlayerController() : nullptr;
	const AIGPlayerCharacter* Player =
		Controller ? Cast<AIGPlayerCharacter>(Controller->GetPawn()) : nullptr;
	if (!Player)
	{
		return false;
	}
	// 감독이 다른 액터를 시점으로 잡은 동안은 폰의 손맛을 얹지 않는다.
	if (Controller->GetViewTarget() != Player)
	{
		return false;
	}
	const FRotator Offset = Player->GetCameraFeelRotation();
	if (!Offset.IsNearlyZero())
	{
		InOutPOV.Rotation = (FQuat(InOutPOV.Rotation) * FQuat(Offset)).Rotator();
	}
	// 잡힌 동안 시야가 좁아지고, 닿는 순간 색이 번지며 가장자리가 어두워진다.
	// 이 모디파이어는 부모의 ModifyCamera를 부르지 않으므로 후처리를 직접 얹는다.
	const float FovScale = Player->GetCaptureFovScale();
	if (!FMath::IsNearlyEqual(FovScale, 1.0f))
	{
		InOutPOV.FOV *= FovScale;
	}
	const float Impact = Player->GetCaptureImpactAlpha();
	const float Tunnel = Player->GetCaptureTunnelAlpha();
	// 어둠 속에 선 것이 커지는 동안에도 가장자리부터 먹힌다. 색 번짐과 흐림은 포획에만 쓴다.
	const float Dread = Player->GetThreatVignetteAlpha();
	const bool bCaptureFeel = Impact > 0.001f || Tunnel > 0.001f;
	if (CameraOwner && (bCaptureFeel || Dread > 0.001f))
	{
		FPostProcessSettings Settings;
		Settings.bOverride_VignetteIntensity = true;
		Settings.VignetteIntensity = FMath::Lerp(0.5f, 1.3f, FMath::Max(Tunnel, Dread));
		if (bCaptureFeel)
		{
			Settings.bOverride_SceneFringeIntensity = true;
			Settings.SceneFringeIntensity = 0.6f + 3.4f * Impact;
			Settings.bOverride_MotionBlurAmount = true;
			// 부딪힌 순간에만 번진다. 달려드는 얼굴은 멈췄다 튀는 동작이라 번지면 뭉개진다.
			Settings.MotionBlurAmount = 0.08f + 0.6f * Impact;
		}
		CameraOwner->AddCachedPPBlend(Settings, 1.0f);
	}
	return false;
}
