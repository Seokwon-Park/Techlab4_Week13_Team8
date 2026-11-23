#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/PhysicsAssetTreePanel.h"
#include "Panels/PhysicsAssetDetailsPanel.h"

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "PhysicsAssetDockSpace", "PhysicsAssetPanelClass")
	, PhysicsEditor(InEditor)
{
	RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetTreePanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetDetailsPanel>(InEditor));
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
	// UE PhAT 배치: 왼쪽 Skeleton Tree, 가운데 뷰포트, 오른쪽 Details
	ImGuiID ViewportId = InDockSpaceId;
	const ImGuiID TreeId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.20f, nullptr, &ViewportId);
	const ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.30f, nullptr, &ViewportId);
	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), TreeId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}
