#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

class FPhysicsAssetTreePanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetTreePanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Skeleton Tree", "PhysicsAssetPanel")
	{
	}

protected:
	void DrawContent() override;

private:
	void DrawBone(int32 BoneIndex);
	void DrawBody(int32 BoneIndex, int32 BodyIndex, bool bNestChildBones);
	void DrawConstraints(int32 BoneIndex);
	bool HasConstraints(int32 BoneIndex) const;
	bool HasBodyDescendant(int32 BoneIndex) const;
	int32 FindBody(int32 BoneIndex) const;

	bool bShowAllBones = false;
};
