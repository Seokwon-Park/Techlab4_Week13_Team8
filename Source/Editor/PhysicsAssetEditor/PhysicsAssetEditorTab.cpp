#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "PhysicsAssetDockSpace", "PhysicsAssetPanelClass")
	, PhysicsEditor(InEditor)
{
	RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
}

void FPhysicsAssetEditorTab::BeforeRender()
{
	// 패널이 그려지지 않은 프레임에는 프리뷰 입력을 비운다
	PhysicsEditor.SetViewportState(0, 0, {});
}

void FPhysicsAssetEditorTab::DrawMenuBar()
{
	FDockedAssetEditorTab::DrawMenuBar();

	if (ImGui::BeginMenu("Show"))
	{
		ImGui::MenuItem("Bodies", nullptr, &PhysicsEditor.bShowBodies);
		ImGui::MenuItem("Constraints", nullptr, &PhysicsEditor.bShowConstraints);
		bool bShowBones = PhysicsEditor.IsBoneVisualizerVisible();
		if (ImGui::MenuItem("Bones", nullptr, &bShowBones))
		{
			PhysicsEditor.SetBoneVisualizerVisible(bShowBones);
		}
		ImGui::EndMenu();
	}
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), InDockSpaceId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}
