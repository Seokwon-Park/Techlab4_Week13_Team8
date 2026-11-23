#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

class USkeletalBodySetup;
class UPhysicsConstraintTemplate;

class FPhysicsAssetDetailsPanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetDetailsPanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Details", "PhysicsAssetPanel")
	{
	}

protected:
	void DrawContent() override;

private:
	void DrawBody(USkeletalBodySetup& BodySetup);
	void DrawConstraint(UPhysicsConstraintTemplate& Template);
};
