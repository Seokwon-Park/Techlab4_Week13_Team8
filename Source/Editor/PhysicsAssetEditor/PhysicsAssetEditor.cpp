#include "EnginePCH.h"
#include "PhysicsAssetEditor.h"
#include "PhysicsAssetEditorTab.h"
#include "PhysicsAssetUtils.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysicsAsset.h"
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
	PhysicsAsset = Mesh->GetPhysicsAsset();
	if (!PhysicsAsset)
	{
		UPhysicsAsset* NewAsset = NewObject<UPhysicsAsset>(Mesh);
		if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(*NewAsset, *Mesh))
		{
			HTR_LOG(Warning, "Physics asset generation created no bodies: {}", GetAssetPath());
		}
		Mesh->SetPhysicsAsset(NewAsset);
		PhysicsAsset = NewAsset;
	}

	return true;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	PhysicsAsset = nullptr;
	FSkeletalPreviewEditor::ShutdownResources();
}

TUniquePtr<IEditorTab> FPhysicsAssetEditor::CreateTab()
{
	return MakeUnique<FPhysicsAssetEditorTab>(*this);
}

int32 FPhysicsAssetEditor::GetSelectedBodyIndex() const
{
	if (!PhysicsAsset || !Mesh || SelectedBoneIndex < 0)
	{
		return INDEX_NONE;
	}

	return PhysicsAsset->FindBodyIndex(FName(Mesh->GetRefSkeleton()[SelectedBoneIndex].Name));
}

int32 FPhysicsAssetEditor::GetSelectedConstraintIndex() const
{
	if (!PhysicsAsset || !Mesh || SelectedBoneIndex < 0)
	{
		return INDEX_NONE;
	}

	// 선택한 본이 자식(Bone1)인 조인트
	const FName BoneName(Mesh->GetRefSkeleton()[SelectedBoneIndex].Name);
	for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
	{
		const UPhysicsConstraintTemplate* Template = PhysicsAsset->ConstraintSetup[i];
		if (Template && Template->DefaultInstance.ConstraintBone1 == BoneName)
		{
			return i;
		}
	}
	return INDEX_NONE;
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
		DebugDraw.DrawPhysicsAssetConstraints(&LineBatcher, MeshComponent, *PhysicsAsset, GetSelectedConstraintIndex());
	}
}
