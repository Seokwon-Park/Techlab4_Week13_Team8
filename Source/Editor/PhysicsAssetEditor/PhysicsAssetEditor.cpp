#include "EnginePCH.h"
#include "PhysicsAssetEditor.h"
#include "PhysicsAssetEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"

bool FPhysicsAssetEditor::InitializeResources()
{
	if (!InitializePreview())
	{
		return false;
	}

	PhysicsAsset = Mesh->PhysicsAsset;
	if (!PhysicsAsset)
	{
		UPhysicsAsset* NewAsset = NewObject<UPhysicsAsset>(Mesh);
		FPhysAssetCreateParams Params;
		// 20cm = 사람 메시 대각선(약 256)의 8%. 작게 import된 메시는 같은 비율로 낮춘다
		const FBox& BindBox = Mesh->GetRenderData().LODRenderData[0].BindAABB;
		Params.MinBoneSize = std::min(Params.MinBoneSize, (BindBox.Max - BindBox.Min).Size() * 0.08f);
		FString Error;
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(NewAsset, Mesh, Params, Error, false))
		{
			HTR_LOG(Warning, "Physics asset generation failed: {} ({})", GetAssetPath(), Error);
		}
		// 실패해도 연결해 둬서 열 때마다 재생성하지 않는다
		Mesh->PhysicsAsset = NewAsset;
		PhysicsAsset = NewAsset;
	}

	return true;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	PhysicsAsset = nullptr;
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
	FSkeletalPreviewEditor::ShutdownResources();
}

TUniquePtr<IEditorTab> FPhysicsAssetEditor::CreateTab()
{
	return MakeUnique<FPhysicsAssetEditorTab>(*this);
}

int32 FPhysicsAssetEditor::FindBoneIndexByName(FName BoneName) const
{
	return Mesh ? Mesh->FindBoneIndex(BoneName.ToString()) : INDEX_NONE;
}

void FPhysicsAssetEditor::SelectBody(int32 BodyIndex)
{
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return;
	}

	SelectBone(FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName));
	SelectionType = EPhysicsAssetSelection::Body;
	SelectionIndex = BodyIndex;
}

void FPhysicsAssetEditor::SelectConstraint(int32 ConstraintIndex)
{
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
	{
		return;
	}

	// SelectBone -> OnBoneSelected가 Body 선택으로 바꾸므로 그 뒤에 덮어쓴다
	SelectBone(FindBoneIndexByName(PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance.ConstraintBone1));
	SelectionType = EPhysicsAssetSelection::Constraint;
	SelectionIndex = ConstraintIndex;
}

void FPhysicsAssetEditor::OnBoneSelected()
{
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
	if (!PhysicsAsset || !Mesh || SelectedBoneIndex < 0)
	{
		return;
	}

	const int32 BodyIndex = PhysicsAsset->FindBodyIndex(FName(Mesh->GetRefSkeleton()[SelectedBoneIndex].Name));
	if (BodyIndex != INDEX_NONE)
	{
		SelectionType = EPhysicsAssetSelection::Body;
		SelectionIndex = BodyIndex;
	}
}

void FPhysicsAssetEditor::OnRenderLine(FLineBatcher& LineBatcher)
{
	FSkeletalPreviewEditor::OnRenderLine(LineBatcher);

	if (!PhysicsAsset || !MeshComponent)
	{
		return;
	}

	FPhysicsDebugDraw DebugDraw;
	if (bShowBodies)
	{
		DebugDraw.DrawPhysicsAssetBodies(&LineBatcher, MeshComponent, *PhysicsAsset, false, GetSelectedBodyIndex());
	}
	if (bShowConstraints)
	{
		DebugDraw.DrawPhysicsAssetConstraints(&LineBatcher, MeshComponent, *PhysicsAsset, GetSelectedConstraintIndex(), ConstraintDrawSize);
	}
}
