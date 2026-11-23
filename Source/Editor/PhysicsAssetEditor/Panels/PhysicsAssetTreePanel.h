#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

// UE PhAT의 Skeleton Tree. 계층은 본 → 바디 → 조인트(이 본이 자식인 조인트)
// 기본은 바디만 보기: 본 행을 숨기고 바디 행을 본 계층대로 잇는다. Show All Bones면 본 행도 그린다
class FPhysicsAssetTreePanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetTreePanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Skeleton Tree", "PhysicsAssetPanel")
	{
	}

protected:
	void DrawContent() override;

private:
	void DrawBone(int32 BoneIndex);
	// bNestChildBones: 바디만 보기에서는 자식 본의 바디를 이 바디 아래에 잇는다
	void DrawBody(int32 BoneIndex, int32 BodyIndex, bool bNestChildBones);
	void DrawConstraints(int32 BoneIndex);
	bool HasConstraints(int32 BoneIndex) const;
	bool HasBodyDescendant(int32 BoneIndex) const;
	int32 FindBody(int32 BoneIndex) const;

	bool bShowAllBones = false;
};
