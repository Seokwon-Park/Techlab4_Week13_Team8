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

	// 메시에 없으면 자동 생성해 연결한다 (저장은 아직 없음)
	PhysicsAsset = Mesh->PhysicsAsset;
	if (!PhysicsAsset)
	{
		UPhysicsAsset* NewAsset = NewObject<UPhysicsAsset>(Mesh);
		FPhysAssetCreateParams Params;
		// 기본값 20은 cm 사람 크기 기준(T포즈 대각선 약 256의 8%). 그보다 작게 들어온 메시는 같은 비율로 낮춘다
		const FBox& BindBox = Mesh->GetRenderData().LODRenderData[0].BindAABB;
		Params.MinBoneSize = std::min(Params.MinBoneSize, (BindBox.Max - BindBox.Min).Size() * 0.08f);
		FString Error;
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(NewAsset, Mesh, Params, Error, false))
		{
			HTR_LOG(Warning, "Physics asset generation failed: {} ({})", GetAssetPath(), Error);
		}
		// 실패해도 연결해 둬서 다시 열 때 매번 재생성하지 않는다
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

	// 본도 같이 선택해 뷰포트 본 강조를 맞춘다. OnBoneSelected가 바디 선택으로 바꿔 준다
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

	// 조인트는 자식 본(Bone1)에 붙어 있다. SelectBone이 바디로 바꾼 선택을 뒤에서 덮어쓴다
	SelectBone(FindBoneIndexByName(PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance.ConstraintBone1));
	SelectionType = EPhysicsAssetSelection::Constraint;
	SelectionIndex = ConstraintIndex;
}

void FPhysicsAssetEditor::OnBoneSelected()
{
	// 뷰포트에서 본을 고르면 그 본의 바디를 선택한다. 바디가 없으면 본만 선택
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
