#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

// 뷰포트 표시 설정: Bodies / Constraints / Bones 표시와 Constraint Draw Size
class FPhysicsAssetSettingsPanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetSettingsPanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Settings", "PhysicsAssetPanel")
	{
	}

protected:
	void DrawContent() override;
};
