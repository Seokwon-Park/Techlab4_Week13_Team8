#include "EnginePCH.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetEditor.h"
#include "Editor/SkeletalPreviewEditor/SkeletalPreviewViewportPanel.h"
#include "Panels/PhysicsAssetTreePanel.h"
#include "Panels/PhysicsAssetDetailsPanel.h"
#include "Panels/PhysicsAssetSettingsPanel.h"
#include "Panels/PhysicsAssetGraphPanel.h"

FPhysicsAssetEditorTab::FPhysicsAssetEditorTab(FPhysicsAssetEditor& InEditor)
	: FDockedAssetEditorTab(InEditor, "PhysicsAssetDockSpace", "PhysicsAssetPanelClass")
	, PhysicsEditor(InEditor)
{
	RegisterPanel(MakeUnique<FSkeletalPreviewViewportPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetTreePanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetDetailsPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetSettingsPanel>(InEditor));
	RegisterPanel(MakeUnique<FPhysicsAssetGraphPanel>(InEditor));
}

void FPhysicsAssetEditorTab::BeforeRender()
{
	// 패널이 그려지지 않은 프레임에는 프리뷰 입력을 비운다
	PhysicsEditor.SetViewportState(0, 0, {});
}

void FPhysicsAssetEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID TreeId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.20f, nullptr, &ViewportId);
	const ImGuiID GraphId = ImGui::DockBuilderSplitNode(TreeId, ImGuiDir_Down, 0.35f, nullptr, &TreeId);
	ImGuiID DetailsId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Right, 0.30f, nullptr, &ViewportId);
	const ImGuiID SettingsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Up, 0.20f, nullptr, &DetailsId);
	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ViewportId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), TreeId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[3]->GetWindowName(), SettingsId);
	ImGui::DockBuilderDockWindow(Panels[4]->GetWindowName(), GraphId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}
