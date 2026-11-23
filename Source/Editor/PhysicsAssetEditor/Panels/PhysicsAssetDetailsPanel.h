#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"

class USkeletalBodySetup;
class UPhysicsConstraintTemplate;

// 선택한 바디 또는 조인트의 값을 편집한다. 값은 저장된 그대로(바디 모양은 본 로컬 단위) 보여 준다 (UE와 같음)
// 그리기는 매 프레임 에셋을 읽으므로 바꾸면 뷰포트에 바로 반영된다
class FPhysicsAssetDetailsPanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetDetailsPanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Details", "PhysicsAssetPanel")
	{
	}

protected:
	void DrawContent() override;

private:
	void DrawBody(USkeletalBodySetup& BodySetup);
	void DrawConstraint(UPhysicsConstraintTemplate& Template);
};
