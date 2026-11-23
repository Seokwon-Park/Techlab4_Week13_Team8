#include "EnginePCH.h"
#include "PhysicsAssetSettingsPanel.h"

void FPhysicsAssetSettingsPanel::DrawContent()
{
	ImGui::SeparatorText("Show");
	ImGui::Checkbox("Bodies", &Editor.bShowBodies);
	ImGui::Checkbox("Constraints", &Editor.bShowConstraints);
	bool bShowBones = Editor.IsBoneVisualizerVisible();
	if (ImGui::Checkbox("Bones", &bShowBones))
	{
		Editor.SetBoneVisualizerVisible(bShowBones);
	}

	ImGui::SeparatorText("Constraints");
	ImGui::SliderFloat("Constraint Draw Size", &Editor.ConstraintDrawSize, 0.0f, 4.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
}
