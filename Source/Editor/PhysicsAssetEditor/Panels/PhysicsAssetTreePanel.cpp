#include "EnginePCH.h"
#include "PhysicsAssetTreePanel.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

namespace
{
	const ImVec4 BodyTextColor(0.3f, 0.8f, 1.0f, 1.0f);

	FName GetBoneName(const FPhysicsAssetEditor& Editor, int32 BoneIndex)
	{
		return FName(Editor.GetMesh()->GetRefSkeleton()[BoneIndex].Name);
	}

	const char* GetBoneDisplayName(const FPhysicsAssetEditor& Editor, FName BoneName)
	{
		const int32 BoneIndex = Editor.FindBoneIndexByName(BoneName);
		return BoneIndex >= 0 ? Editor.GetBoneItems()[BoneIndex].Name.c_str() : "?";
	}

	const ImGuiTreeNodeFlags BranchFlags = ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
}

void FPhysicsAssetTreePanel::DrawContent()
{
	if (!Editor.GetMesh() || !Editor.GetPhysicsAsset())
	{
		return;
	}

	ImGui::Checkbox("Show All Bones", &bShowAllBones);
	ImGui::Separator();

	if (ImGui::BeginChild("PhysicsAssetTreeChild", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
	{
		ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, 12.0f);
		for (int32 RootBoneIndex : Editor.GetRootBoneIndices())
		{
			DrawBone(RootBoneIndex);
		}
		ImGui::PopStyleVar();
	}
	ImGui::EndChild();

	switch (PendingAction)
	{
	case EPendingAction::AddBody:          Editor.MakeNewBody(PendingIndex); break;
	case EPendingAction::DeleteBody:       Editor.DeleteBody(PendingIndex); break;
	case EPendingAction::DeleteConstraint: Editor.DeleteConstraint(PendingIndex); break;
	default: break;
	}
	PendingAction = EPendingAction::None;
	PendingIndex = INDEX_NONE;
}

int32 FPhysicsAssetTreePanel::FindBody(int32 BoneIndex) const
{
	return Editor.GetPhysicsAsset()->FindBodyIndex(GetBoneName(Editor, BoneIndex));
}

bool FPhysicsAssetTreePanel::HasConstraints(int32 BoneIndex) const
{
	const FName BoneName = GetBoneName(Editor, BoneIndex);
	for (const UPhysicsConstraintTemplate* Template : Editor.GetPhysicsAsset()->ConstraintSetup)
	{
		if (Template && Template->DefaultInstance.ConstraintBone1 == BoneName)
		{
			return true;
		}
	}
	return false;
}

bool FPhysicsAssetTreePanel::HasBodyDescendant(int32 BoneIndex) const
{
	for (int32 ChildIndex : Editor.GetBoneItems()[BoneIndex].ChildBoneIndices)
	{
		if (FindBody(ChildIndex) != INDEX_NONE || HasBodyDescendant(ChildIndex))
		{
			return true;
		}
	}
	return false;
}

void FPhysicsAssetTreePanel::DrawBone(int32 BoneIndex)
{
	const TArray<FEditorBoneItem>& BoneItems = Editor.GetBoneItems();
	const int32 BodyIndex = FindBody(BoneIndex);

	ImGui::PushID(BoneIndex);

	if (!bShowAllBones)
	{
		// 바디가 없는 본은 행 없이 자식만 같은 깊이로 그린다
		if (BodyIndex != INDEX_NONE)
		{
			DrawBody(BoneIndex, BodyIndex, true);
		}
		else
		{
			for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
			{
				DrawBone(ChildIndex);
			}
		}
		ImGui::PopID();
		return;
	}

	ImGuiTreeNodeFlags Flags = BranchFlags;
	if (BodyIndex == INDEX_NONE && BoneItems[BoneIndex].ChildBoneIndices.IsEmpty())
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (Editor.GetSelectionType() == EPhysicsAssetSelection::None && Editor.GetSelectedBoneIndex() == BoneIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	const bool bOpen = ImGui::TreeNodeEx("Bone", Flags, "%s", BoneItems[BoneIndex].Name.c_str());
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		Editor.SelectBone(BoneIndex);
	}
	if (ImGui::BeginPopupContextItem("BoneContext"))
	{
		if (ImGui::MenuItem("Add Body", nullptr, false, BodyIndex == INDEX_NONE))
		{
			PendingAction = EPendingAction::AddBody;
			PendingIndex = BoneIndex;
		}
		ImGui::EndPopup();
	}

	if (bOpen)
	{
		if (BodyIndex != INDEX_NONE)
		{
			DrawBody(BoneIndex, BodyIndex, false);
		}
		for (int32 ChildIndex : BoneItems[BoneIndex].ChildBoneIndices)
		{
			DrawBone(ChildIndex);
		}
		ImGui::TreePop();
	}
	ImGui::PopID();
}

void FPhysicsAssetTreePanel::DrawBody(int32 BoneIndex, int32 BodyIndex, bool bNestChildBones)
{
	ImGuiTreeNodeFlags Flags = BranchFlags;
	if (!HasConstraints(BoneIndex) && !(bNestChildBones && HasBodyDescendant(BoneIndex)))
	{
		Flags |= ImGuiTreeNodeFlags_Leaf;
	}
	if (Editor.GetSelectedBodyIndex() == BodyIndex)
	{
		Flags |= ImGuiTreeNodeFlags_Selected;
	}

	ImGui::PushStyleColor(ImGuiCol_Text, BodyTextColor);
	const bool bOpen = ImGui::TreeNodeEx("Body", Flags, "%s", Editor.GetBoneItems()[BoneIndex].Name.c_str());
	ImGui::PopStyleColor();
	if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen())
	{
		Editor.SelectBody(BodyIndex);
	}
	if (ImGui::BeginPopupContextItem("BodyContext"))
	{
		if (ImGui::MenuItem("Delete Body"))
		{
			PendingAction = EPendingAction::DeleteBody;
			PendingIndex = BodyIndex;
		}
		ImGui::EndPopup();
	}

	if (bOpen)
	{
		DrawConstraints(BoneIndex);
		if (bNestChildBones)
		{
			for (int32 ChildIndex : Editor.GetBoneItems()[BoneIndex].ChildBoneIndices)
			{
				DrawBone(ChildIndex);
			}
		}
		ImGui::TreePop();
	}
}

void FPhysicsAssetTreePanel::DrawConstraints(int32 BoneIndex)
{
	const UPhysicsAsset* PhysicsAsset = Editor.GetPhysicsAsset();
	const FName BoneName = GetBoneName(Editor, BoneIndex);
	const ImGuiTreeNodeFlags LeafFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanAvailWidth;

	for (int32 ConstraintIndex = 0; ConstraintIndex < PhysicsAsset->ConstraintSetup.Num(); ++ConstraintIndex)
	{
		const FConstraintInstance& Constraint = PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance;
		if (!(Constraint.ConstraintBone1 == BoneName))
		{
			continue;
		}

		ImGuiTreeNodeFlags Flags = LeafFlags;
		if (Editor.GetSelectedConstraintIndex() == ConstraintIndex)
		{
			Flags |= ImGuiTreeNodeFlags_Selected;
		}
		ImGui::PushID(ConstraintIndex);
		ImGui::TreeNodeEx("Constraint", Flags, "[ %s -> %s ] Constraint",
			GetBoneDisplayName(Editor, Constraint.ConstraintBone2), GetBoneDisplayName(Editor, Constraint.ConstraintBone1));
		if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
		{
			Editor.SelectConstraint(ConstraintIndex);
		}
		if (ImGui::BeginPopupContextItem("ConstraintContext"))
		{
			if (ImGui::MenuItem("Delete Constraint"))
			{
				PendingAction = EPendingAction::DeleteConstraint;
				PendingIndex = ConstraintIndex;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
}
