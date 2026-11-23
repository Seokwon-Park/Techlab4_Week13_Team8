#pragma once

#include "Editor/AssetEditor/DockedAssetEditorTab.h"

class FPhysicsAssetEditor;

class FPhysicsAssetEditorTab : public FDockedAssetEditorTab
{
public:
	explicit FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor);

private:
	void BeforeRender() override;
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size) override;

	FPhysicsAssetEditor& PhysicsEditor;
};
