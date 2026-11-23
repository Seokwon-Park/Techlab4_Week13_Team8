#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"

class UPhysicsAsset;

enum class EPhysicsAssetSelection : uint8 { None, Body, Constraint };

class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	// SkeletalMesh 에디터와 같은 메시 경로라 접미사로 구분한다
	static FString MakeDocumentKey(const FString& MeshPath) { return MeshPath + ".physicsasset"; }

	FPhysicsAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InMeshPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, MakeDocumentKey(InMeshPath))
	{
		Mesh = &InMesh;
	}

	const char* GetEditorTypeName() const override { return "PhysicsAsset"; }

	void OnRenderLine(FLineBatcher& LineBatcher) override;

	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }

	void SelectBody(int32 BodyIndex);
	void SelectConstraint(int32 ConstraintIndex);
	EPhysicsAssetSelection GetSelectionType() const { return SelectionType; }
	int32 GetSelectedBodyIndex() const { return SelectionType == EPhysicsAssetSelection::Body ? SelectionIndex : INDEX_NONE; }
	int32 GetSelectedConstraintIndex() const { return SelectionType == EPhysicsAssetSelection::Constraint ? SelectionIndex : INDEX_NONE; }
	int32 FindBoneIndexByName(FName BoneName) const;

	bool bShowBodies = true;
	bool bShowConstraints = true;
	float ConstraintDrawSize = 1.0f;

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;
	void OnBoneSelected() override;
	void TickAfterWorld(float DeltaTime) override;

private:
	// 레이에 맞은 Body·Constraint 중 가장 가까운 것. Constraint가 맞으면 Body보다 우선한다
	bool PickPhysics(const struct FRay& Ray, EPhysicsAssetSelection& OutType, int32& OutIndex) const;

	UPhysicsAsset* PhysicsAsset = nullptr;
	EPhysicsAssetSelection SelectionType = EPhysicsAssetSelection::None;
	int32 SelectionIndex = INDEX_NONE;
};
