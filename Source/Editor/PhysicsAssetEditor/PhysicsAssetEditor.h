#pragma once

#include "Editor/SkeletalPreviewEditor/SkeletalPreviewEditor.h"
#include "Editor/Gizmo/Gizmo.h"
#include "Editor/EditorModeManager.h"
#include "Physics/AggregateGeom.h"
#include "Physics/ConstraintInstance.h"

class UPhysicsAsset;
class UPhysicsConstraintTemplate;
class USkeletalBodySetup;

enum class EPhysicsAssetSelection : uint8 { None, Body, Constraint };

class FPhysicsAssetEditor : public FSkeletalPreviewEditor
{
public:
	// SkeletalMesh 에디터와 같은 메시 경로라 접미사로 구분한다
	static FString MakeDocumentKey(const FString& MeshPath) { return MeshPath + ".physicsasset"; }

	FPhysicsAssetEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager,
		const FString& InMeshPath, USkeletalMesh& InMesh)
		: FSkeletalPreviewEditor(InEditorEngine, InEditorManager, MakeDocumentKey(InMeshPath))
		, MeshPath(InMeshPath)
	{
		Mesh = &InMesh;
	}

	const char* GetEditorTypeName() const override { return "PhysicsAsset"; }

	void OnRenderLine(FLineBatcher& LineBatcher) override;

	const FGizmo* GetPreviewGizmo() const override { return PhysicsGizmo.GetTarget() ? &PhysicsGizmo : nullptr; }
	UE::Widget::EWidgetMode GetGizmoMode() const override { return ModeTools.GetWidgetMode(); }
	void SetGizmoMode(UE::Widget::EWidgetMode Mode) override;
	bool IsGizmoDragging() const override { return PhysicsGizmo.IsUsing(); }

	UPhysicsAsset* GetPhysicsAsset() const { return PhysicsAsset; }

	bool IsDirty() const override { return bDirty; }
	bool ConfirmClose() override;
	void MarkDirty() { bDirty = true; }
	bool Save();

	void SelectBody(int32 BodyIndex);
	void SelectConstraint(int32 ConstraintIndex);
	EPhysicsAssetSelection GetSelectionType() const { return SelectionType; }
	int32 GetSelectedBodyIndex() const { return SelectionType == EPhysicsAssetSelection::Body ? SelectionIndex : INDEX_NONE; }
	int32 GetSelectedConstraintIndex() const { return SelectionType == EPhysicsAssetSelection::Constraint ? SelectionIndex : INDEX_NONE; }
	int32 FindBoneIndexByName(FName BoneName) const;

	// 가장 가까운 조상 바디와 Constraint로 잇고, 자식 바디의 Constraint도 새 바디로 다시 잇는다
	void MakeNewBody(int32 BoneIndex);
	// 자식 바디의 Constraint를 지울 바디의 부모 바디로 옮긴 뒤 지운다
	void DeleteBody(int32 BodyIndex);
	void DeleteConstraint(int32 ConstraintIndex);

	bool bShowBodies = true;
	bool bShowConstraints = true;
	float ConstraintDrawSize = 1.0f;

protected:
	TUniquePtr<IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;
	void OnBoneSelected() override;
	void TickAfterWorld(float DeltaTime) override;
	bool IsViewportInteractionBusy() const override { return PhysicsGizmo.IsUsing(); }
	void CancelViewportInteraction() override { PhysicsGizmo.EndDrag(); }

private:
	void UpdatePhysicsPicking();
	USkeletalBodySetup* GetSelectedBodySetup() const;
	// 선택한 바디의 첫 Shape (Sphyl, Box, Sphere 순)의 본 로컬 위치와 회전
	bool GetSelectedShapeLocal(FVector& OutCenter, FQuat& OutRotation) const;
	UPhysicsConstraintTemplate* GetSelectedConstraintTemplate() const;
	bool GetShapeGizmoTransform(FTransform& OutTransform) const;
	// 부모 쪽 프레임(Frame2) 위치. 크기 모드에서는 기즈모를 숨긴다
	bool GetConstraintGizmoTransform(FTransform& OutTransform) const;
	void SyncPhysicsGizmo();
	bool UpdatePhysicsGizmo();
	bool BeginShapeDrag();
	bool BeginConstraintDrag();
	void ApplyShapeTransform();
	void ApplyConstraintTransform();
	// 레이에 맞은 Body·Constraint 중 가장 가까운 것. Constraint가 맞으면 Body보다 우선한다
	bool PickPhysics(const struct FRay& Ray, EPhysicsAssetSelection& OutType, int32& OutIndex) const;
	FString GetSaveFilePath() const;
	void ClearSelection();
	bool IsBoneChildOf(int32 BoneIndex, int32 AncestorBoneIndex) const;
	// 자기 자신부터 위로 올라가며 처음 만나는 바디
	int32 FindControllingBodyIndex(int32 BoneIndex) const;
	int32 FindChildConstraintIndex(FName ChildBoneName) const;
	// bKeepProfile이면 각도 제한 설정은 그대로 두고 본과 프레임만 다시 잡는다
	void InitConstraintSetup(UPhysicsConstraintTemplate* Template, int32 ChildBodyIndex, int32 ParentBodyIndex, bool bKeepProfile);
	void RevertFromDisk();

	FString MeshPath;
	bool bDirty = false;

	UPhysicsAsset* PhysicsAsset = nullptr;
	EPhysicsAssetSelection SelectionType = EPhysicsAssetSelection::None;
	int32 SelectionIndex = INDEX_NONE;

	USceneComponent* GizmoTarget = nullptr;
	// PhysicsGizmo가 생성자에서 포인터를 받으므로 먼저 선언해야 한다
	FEditorModeTools ModeTools;
	FGizmo PhysicsGizmo{ &ModeTools };
	FKAggregateGeom DragStartGeom;
	FConstraintInstance DragStartConstraint;
	FMatrix DragBoneWorldInverse;
	FQuat DragBoneWorldRotation;
	FMatrix DragChildBoneWorldInverse;
	FMatrix DragChildFrameRelative;
};
