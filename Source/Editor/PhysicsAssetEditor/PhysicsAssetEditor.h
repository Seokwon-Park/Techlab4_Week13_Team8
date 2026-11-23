#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"

class UPhysicsAsset;

enum class EPhysicsAssetSelection : uint8 { None, Body, Constraint };

// UE Physics Asset Editor(PhAT). USkeletalMesh::PhysicsAsset을 편집한다
class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	// 메시 경로 뒤에 접미사를 붙여 SkeletalMesh 에디터와 다른 문서로 구분한다
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

	// 바디와 조인트는 따로 선택한다 (UE PhAT와 같음). 고르면 해당 본도 선택된다
	void SelectBody(int32 BodyIndex);
	void SelectConstraint(int32 ConstraintIndex);
	EPhysicsAssetSelection GetSelectionType() const { return SelectionType; }
	int32 GetSelectedBodyIndex() const { return SelectionType == EPhysicsAssetSelection::Body ? SelectionIndex : INDEX_NONE; }
	int32 GetSelectedConstraintIndex() const { return SelectionType == EPhysicsAssetSelection::Constraint ? SelectionIndex : INDEX_NONE; }
	int32 FindBoneIndexByName(FName BoneName) const;

	bool bShowBodies = true;
	bool bShowConstraints = true;

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;
	void OnBoneSelected() override;

private:
	UPhysicsAsset* PhysicsAsset = nullptr;
	EPhysicsAssetSelection SelectionType = EPhysicsAssetSelection::None;
	int32 SelectionIndex = INDEX_NONE;
};
