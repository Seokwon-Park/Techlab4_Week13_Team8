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
	// UE와 같은 범위 0~4. 기본 크기는 메시 대각선 비율이라 메시 단위와 상관없이 1이 기본
	ImGui::SliderFloat("Constraint Draw Size", &Editor.ConstraintDrawSize, 0.0f, 4.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
}
