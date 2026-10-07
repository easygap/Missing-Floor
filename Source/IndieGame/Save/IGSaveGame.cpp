#include "Save/IGSaveGame.h"
#include "Player/IGFlashlightComponent.h"

UIGSaveGame::UIGSaveGame()
{
	Progress.SchemaVersion = CurrentSchemaVersion;
}

void UIGSaveGame::MigrateFlashlightOwnership(FIGProgressSnapshot& Snapshot)
{
	if (Snapshot.SchemaVersion < 4 && Snapshot.MissingFloorNarrative.Night.NightIndex > 0)
	{
		Snapshot.StoryStateTags.AddTag(UIGFlashlightComponent::GetOwnershipTag());
	}
	Snapshot.SchemaVersion = CurrentSchemaVersion;
}
