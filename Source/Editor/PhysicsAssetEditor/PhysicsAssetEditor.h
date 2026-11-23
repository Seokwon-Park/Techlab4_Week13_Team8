#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"

class UPhysicsAsset;

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

	int32 GetSelectedBodyIndex() const;
	int32 GetSelectedConstraintIndex() const;

	bool bShowBodies = true;
	bool bShowConstraints = true;

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;

private:
	UPhysicsAsset* PhysicsAsset = nullptr;
};
