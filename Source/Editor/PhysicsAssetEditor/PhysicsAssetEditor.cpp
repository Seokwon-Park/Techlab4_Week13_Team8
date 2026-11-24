#include "EnginePCH.h"
#include "PhysicsAssetEditor.h"
#include "PhysicsAssetEditorTab.h"
#include "Components/SkeletalMeshComponent.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Physics/PhysicsDebugDraw.h"
#include "Render/SkeletalMesh.h"
#include "Render/LineBatcher.h"
#include "Collision/Ray.h"

namespace
{
	constexpr float MinShapeSize = 0.5f;

	// 축 하나만 끌면 그 축만 1에서 벗어나므로 1에서 가장 먼 값을 쓴다
	float DominantScale(float A, float B)
	{
		return std::abs(A - 1.0f) >= std::abs(B - 1.0f) ? A : B;
	}

	// 본 스케일이 섞인 행렬일 수 있으므로 축은 정규화한다
	void SetRefFrame(FConstraintInstance& Constraint, int32 Frame, const FMatrix& BoneLocalFrame)
	{
		const FVector Pos(BoneLocalFrame.M[3][0], BoneLocalFrame.M[3][1], BoneLocalFrame.M[3][2]);
		const FVector Pri = FVector(BoneLocalFrame.M[0][0], BoneLocalFrame.M[0][1], BoneLocalFrame.M[0][2]).GetSafeNormal();
		const FVector Sec = FVector(BoneLocalFrame.M[1][0], BoneLocalFrame.M[1][1], BoneLocalFrame.M[1][2]).GetSafeNormal();
		if (Frame == 0)
		{
			Constraint.Pos1 = Pos;
			Constraint.PriAxis1 = Pri;
			Constraint.SecAxis1 = Sec;
		}
		else
		{
			Constraint.Pos2 = Pos;
			Constraint.PriAxis2 = Pri;
			Constraint.SecAxis2 = Sec;
		}
	}
}

bool FPhysicsAssetEditor::InitializeResources()
{
	if (!InitializePreview())
	{
		return false;
	}

	GizmoTarget = Cast<USceneComponent>(PreviewScene.AddComponent(USceneComponent::StaticClass()));
	if (!GizmoTarget)
	{
		return false;
	}
	GizmoTarget->DetachFromParent();

	PhysicsAsset = Mesh->GetPhysicsAsset();
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
		bDirty = true;
	}

	return true;
}

FString FPhysicsAssetEditor::GetSaveFilePath() const
{
	if (!Mesh->PhysicsAssetPath.empty())
	{
		return Mesh->PhysicsAssetPath;
	}
	// USkeletalMesh::GetPhysicsAsset이 읽는 기본 위치와 같아야 다음에 열 때 로드된다
	return (std::filesystem::path("Assets/Physics") /
		(std::filesystem::path(Mesh->GetPath()).stem().string() + ".physicsasset")).generic_string();
}

bool FPhysicsAssetEditor::Save()
{
	if (!PhysicsAsset)
	{
		return false;
	}

	const FString FilePath = GetSaveFilePath();
	PhysicsAsset->PreviewMeshPath = MeshPath;
	FString Error;
	if (!PhysicsAssetFile::Write(FilePath, *PhysicsAsset, Error))
	{
		HTR_LOG(Warning, "Failed to save physics asset: {} ({})", FilePath, Error);
		return false;
	}
	Mesh->PhysicsAssetPath = FilePath;
	bDirty = false;
	HTR_LOG(Info, "Saved physics asset: {}", FilePath);
	return true;
}

bool FPhysicsAssetEditor::ConfirmClose()
{
	if (!bDirty)
	{
		return true;
	}

	const int Result = MessageBoxW(GetActiveWindow(), L"Save changes to this physics asset?",
		L"Physics Asset", MB_YESNOCANCEL | MB_ICONQUESTION);
	if (Result == IDCANCEL)
	{
		return false;
	}
	if (Result == IDYES)
	{
		return Save();
	}

	// 메시와 공유하는 객체라 저장 안 한 편집이 남으면 레벨 시뮬레이션에 그대로 쓰인다
	RevertFromDisk();
	return true;
}

void FPhysicsAssetEditor::RevertFromDisk()
{
	const FString FilePath = GetSaveFilePath();
	if (!PhysicsAsset || !std::filesystem::exists(FilePath))
	{
		return;
	}

	FString Error;
	if (!PhysicsAssetFile::Read(FilePath, *PhysicsAsset, Error))
	{
		HTR_LOG(Warning, "Failed to revert physics asset: {} ({})", FilePath, Error);
	}
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
	bDirty = false;
}

void FPhysicsAssetEditor::ShutdownResources()
{
	PhysicsGizmo.EndDrag();
	PhysicsGizmo.SetTarget(nullptr);
	GizmoTarget = nullptr;
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

void FPhysicsAssetEditor::ClearSelection()
{
	SelectionType = EPhysicsAssetSelection::None;
	SelectionIndex = INDEX_NONE;
}

bool FPhysicsAssetEditor::IsBoneChildOf(int32 BoneIndex, int32 AncestorBoneIndex) const
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	for (int32 Parent = Bones[BoneIndex].ParentIndex; Parent >= 0; Parent = Bones[Parent].ParentIndex)
	{
		if (Parent == AncestorBoneIndex)
		{
			return true;
		}
	}
	return false;
}

int32 FPhysicsAssetEditor::FindControllingBodyIndex(int32 BoneIndex) const
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	for (int32 Bone = BoneIndex; Bone >= 0; Bone = Bones[Bone].ParentIndex)
	{
		const int32 BodyIndex = PhysicsAsset->FindBodyIndex(FName(Bones[Bone].Name));
		if (BodyIndex != INDEX_NONE)
		{
			return BodyIndex;
		}
	}
	return INDEX_NONE;
}

int32 FPhysicsAssetEditor::FindChildConstraintIndex(FName ChildBoneName) const
{
	for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
	{
		if (PhysicsAsset->ConstraintSetup[i]->DefaultInstance.ConstraintBone1 == ChildBoneName)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

void FPhysicsAssetEditor::InitConstraintSetup(UPhysicsConstraintTemplate* Template, int32 ChildBodyIndex, int32 ParentBodyIndex, bool bKeepProfile)
{
	const int32 ChildBone = FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[ChildBodyIndex]->BoneName);
	const int32 ParentBone = FindBoneIndexByName(PhysicsAsset->SkeletalBodySetups[ParentBodyIndex]->BoneName);
	if (ChildBone == INDEX_NONE || ParentBone == INDEX_NONE)
	{
		return;
	}

	FConstraintInstance& Constraint = Template->DefaultInstance;
	const FConstraintProfileProperties Profile = Constraint.ProfileInstance;
	FPhysicsAssetUtils::InitConstraintFromBones(Constraint, Mesh->GetRefSkeleton(), ChildBone, ParentBone, FPhysAssetCreateParams());
	if (bKeepProfile)
	{
		Constraint.ProfileInstance = Profile;
	}
	PhysicsAsset->DisableCollision(ChildBodyIndex, ParentBodyIndex);
}

void FPhysicsAssetEditor::MakeNewBody(int32 BoneIndex)
{
	const TArray<FBoneInfo>& Bones = Mesh->GetRefSkeleton();
	if (!PhysicsAsset || BoneIndex < 0 || BoneIndex >= Bones.Num())
	{
		return;
	}
	const FName BoneName(Bones[BoneIndex].Name);
	if (PhysicsAsset->FindBodyIndex(BoneName) != INDEX_NONE)
	{
		return;
	}

	const int32 ParentBodyIndex = FindControllingBodyIndex(BoneIndex);
	const int32 NewBodyIndex = FPhysicsAssetUtils::CreateNewBody(PhysicsAsset, BoneName, FPhysAssetCreateParams());
	if (!FPhysicsAssetUtils::CreateCollisionFromBone(PhysicsAsset->SkeletalBodySetups[NewBodyIndex], Mesh, BoneIndex))
	{
		FPhysicsAssetUtils::DestroyBody(PhysicsAsset, NewBodyIndex);
		HTR_LOG(Warning, "Cannot add a body: bone has no skinned vertices: {}", Bones[BoneIndex].Name);
		return;
	}

	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		if (!IsBoneChildOf(i, BoneIndex))
		{
			continue;
		}
		const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(FName(Bones[i].Name));
		if (ChildBodyIndex == INDEX_NONE)
		{
			continue;
		}

		int32 ConstraintIndex = FindChildConstraintIndex(FName(Bones[i].Name));
		const bool bNewConstraint = ConstraintIndex == INDEX_NONE;
		if (bNewConstraint)
		{
			ConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, FName(Bones[i].Name));
		}
		else
		{
			const FName OldParentName = PhysicsAsset->ConstraintSetup[ConstraintIndex]->DefaultInstance.ConstraintBone2;
			const int32 OldParentBone = FindBoneIndexByName(OldParentName);
			if (OldParentBone != INDEX_NONE && IsBoneChildOf(OldParentBone, BoneIndex))
			{
				continue;
			}
			const int32 OldParentBody = PhysicsAsset->FindBodyIndex(OldParentName);
			if (OldParentBody != INDEX_NONE && OldParentBody == ParentBodyIndex)
			{
				PhysicsAsset->EnableCollision(ChildBodyIndex, OldParentBody);
			}
		}
		InitConstraintSetup(PhysicsAsset->ConstraintSetup[ConstraintIndex], ChildBodyIndex, NewBodyIndex, !bNewConstraint);
	}

	if (ParentBodyIndex != INDEX_NONE)
	{
		const int32 ConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, BoneName);
		InitConstraintSetup(PhysicsAsset->ConstraintSetup[ConstraintIndex], NewBodyIndex, ParentBodyIndex, false);
	}

	SelectBody(NewBodyIndex);
	MarkDirty();
}

void FPhysicsAssetEditor::DeleteBody(int32 BodyIndex)
{
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return;
	}

	const FName BoneName = PhysicsAsset->SkeletalBodySetups[BodyIndex]->BoneName;
	const int32 BoneIndex = FindBoneIndexByName(BoneName);
	const int32 ParentBone = BoneIndex != INDEX_NONE ? Mesh->GetRefSkeleton()[BoneIndex].ParentIndex : INDEX_NONE;
	const int32 ParentBodyIndex = ParentBone >= 0 ? FindControllingBodyIndex(ParentBone) : INDEX_NONE;
	if (ParentBodyIndex != INDEX_NONE)
	{
		TArray<int32> Constraints;
		PhysicsAsset->BodyFindConstraints(BodyIndex, Constraints);
		for (int32 ConstraintIndex : Constraints)
		{
			UPhysicsConstraintTemplate* Constraint = PhysicsAsset->ConstraintSetup[ConstraintIndex];
			if (!(Constraint->DefaultInstance.ConstraintBone2 == BoneName))
			{
				continue;
			}
			const FName ChildName = Constraint->DefaultInstance.ConstraintBone1;
			const int32 ChildBodyIndex = PhysicsAsset->FindBodyIndex(ChildName);
			if (ChildBodyIndex == INDEX_NONE)
			{
				continue;
			}
			const int32 NewConstraintIndex = FPhysicsAssetUtils::CreateNewConstraint(PhysicsAsset, ChildName, Constraint);
			InitConstraintSetup(PhysicsAsset->ConstraintSetup[NewConstraintIndex], ChildBodyIndex, ParentBodyIndex, true);
		}
	}

	FPhysicsAssetUtils::DestroyBody(PhysicsAsset, BodyIndex);
	ClearSelection();
	MarkDirty();
}

void FPhysicsAssetEditor::DeleteConstraint(int32 ConstraintIndex)
{
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
	{
		return;
	}

	FPhysicsAssetUtils::DestroyConstraint(PhysicsAsset, ConstraintIndex);
	ClearSelection();
	MarkDirty();
}

void FPhysicsAssetEditor::OnBoneSelected()
{
	PhysicsGizmo.EndDrag();
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
		DebugDraw.DrawPhysicsAssetBodies(&LineBatcher, MeshComponent, *PhysicsAsset, false, GetSelectedBodyIndex(), true);
	}
	if (bShowConstraints)
	{
		DebugDraw.DrawPhysicsAssetConstraints(&LineBatcher, MeshComponent, *PhysicsAsset, GetSelectedConstraintIndex(), ConstraintDrawSize);
	}
}

void FPhysicsAssetEditor::TickAfterWorld(float DeltaTime)
{
	if (!UpdatePhysicsGizmo())
	{
		UpdatePhysicsPicking();
	}
	SyncPhysicsGizmo();
}

void FPhysicsAssetEditor::UpdatePhysicsPicking()
{
	const bool bClicked = CanProcessViewportInput() && MeshComponent && PhysicsAsset &&
		PointerInput.bHovered && PointerInput.bMousePressed && !PointerInput.bCancel &&
		!CameraInput.bCaptured && ViewportWidth > 0 && ViewportHeight > 0;
	if (bClicked)
	{
		const FVector2 MousePosition(PointerInput.MouseUV.X * ViewportWidth, PointerInput.MouseUV.Y * ViewportHeight);
		EPhysicsAssetSelection HitType = EPhysicsAssetSelection::None;
		int32 HitIndex = INDEX_NONE;
		if (PickPhysics(ViewportClient->DeprojectScreenToWorld(MousePosition), HitType, HitIndex))
		{
			HitType == EPhysicsAssetSelection::Body ? SelectBody(HitIndex) : SelectConstraint(HitIndex);
			return;
		}
	}

	// 아무것도 맞지 않으면 본을 고른다
	UpdateBonePicking();
}

void FPhysicsAssetEditor::SetGizmoMode(UE::Widget::EWidgetMode Mode)
{
	PhysicsGizmo.EndDrag();
	ModeTools.SetWidgetMode(Mode);
	SyncPhysicsGizmo();
}

USkeletalBodySetup* FPhysicsAssetEditor::GetSelectedBodySetup() const
{
	const int32 BodyIndex = GetSelectedBodyIndex();
	if (!PhysicsAsset || BodyIndex < 0 || BodyIndex >= PhysicsAsset->SkeletalBodySetups.Num())
	{
		return nullptr;
	}
	return PhysicsAsset->SkeletalBodySetups[BodyIndex];
}

bool FPhysicsAssetEditor::GetSelectedShapeLocal(FVector& OutCenter, FQuat& OutRotation) const
{
	const USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	if (!BodySetup)
	{
		return false;
	}

	const FKAggregateGeom& Geom = BodySetup->AggGeom;
	if (!Geom.SphylElems.IsEmpty())
	{
		OutCenter = Geom.SphylElems[0].Center;
		OutRotation = Geom.SphylElems[0].Rotation.Quaternion();
		return true;
	}
	if (!Geom.BoxElems.IsEmpty())
	{
		OutCenter = Geom.BoxElems[0].Center;
		OutRotation = Geom.BoxElems[0].Rotation.Quaternion();
		return true;
	}
	if (!Geom.SphereElems.IsEmpty())
	{
		OutCenter = Geom.SphereElems[0].Center;
		OutRotation = FQuat::Identity();
		return true;
	}
	return false;
}

UPhysicsConstraintTemplate* FPhysicsAssetEditor::GetSelectedConstraintTemplate() const
{
	const int32 ConstraintIndex = GetSelectedConstraintIndex();
	if (!PhysicsAsset || ConstraintIndex < 0 || ConstraintIndex >= PhysicsAsset->ConstraintSetup.Num())
	{
		return nullptr;
	}
	return PhysicsAsset->ConstraintSetup[ConstraintIndex];
}

bool FPhysicsAssetEditor::GetShapeGizmoTransform(FTransform& OutTransform) const
{
	const USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	FTransform BoneWorld;
	FVector Center;
	FQuat Rotation;
	if (!BodySetup ||
		!FPhysicsDebugDraw().GetBoneWorldTransform(MeshComponent, BodySetup->BoneName.ToString(), BoneWorld) ||
		!GetSelectedShapeLocal(Center, Rotation))
	{
		return false;
	}

	OutTransform = FTransform((BoneWorld.Rotation * Rotation).Normalized(), BoneWorld.GetLocalMatrix().TransformPosition(Center));
	return true;
}

bool FPhysicsAssetEditor::GetConstraintGizmoTransform(FTransform& OutTransform) const
{
	const UPhysicsConstraintTemplate* Template = GetSelectedConstraintTemplate();
	FMatrix ParentFrameWorld, ChildFrameWorld;
	if (!Template || ModeTools.GetWidgetMode() == UE::Widget::WM_Scale ||
		!FPhysicsDebugDraw().GetConstraintFrames(MeshComponent, Template->DefaultInstance, ParentFrameWorld, ChildFrameWorld))
	{
		return false;
	}

	// 본 스케일이 프레임 축에 섞이지 않도록 위치와 회전만 쓴다
	const FTransform ParentFrame = FTransform::FromMatrix(ParentFrameWorld);
	OutTransform = FTransform(ParentFrame.Rotation, ParentFrame.Location);
	return true;
}

void FPhysicsAssetEditor::SyncPhysicsGizmo()
{
	FTransform GizmoTransform;
	const bool bHasTarget = MeshComponent && GizmoTarget &&
		(SelectionType == EPhysicsAssetSelection::Constraint
			? GetConstraintGizmoTransform(GizmoTransform)
			: GetShapeGizmoTransform(GizmoTransform));
	if (!bHasTarget)
	{
		PhysicsGizmo.SetTarget(nullptr);
		return;
	}

	if (PhysicsGizmo.IsUsing())
	{
		return;
	}

	// 스케일은 1로 두고 드래그 동안 늘어난 배율만 읽는다
	GizmoTarget->SetTransform(GizmoTransform);
	PhysicsGizmo.SetTarget(GizmoTarget);
}

bool FPhysicsAssetEditor::BeginShapeDrag()
{
	USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	FTransform BoneWorld;
	if (!BodySetup || !FPhysicsDebugDraw().GetBoneWorldTransform(MeshComponent, BodySetup->BoneName.ToString(), BoneWorld))
	{
		return false;
	}

	const FMatrix BoneMatrix = BoneWorld.GetLocalMatrix();
	if (fabsf(BoneMatrix.Determinant()) < 1e-12f)
	{
		return false;
	}

	DragStartGeom = BodySetup->AggGeom;
	DragBoneWorldInverse = BoneMatrix.Inverse();
	DragBoneWorldRotation = BoneWorld.Rotation;
	return true;
}

bool FPhysicsAssetEditor::BeginConstraintDrag()
{
	UPhysicsConstraintTemplate* Template = GetSelectedConstraintTemplate();
	FTransform ParentFrame;
	FMatrix ParentFrameWorld, ChildFrameWorld;
	if (!Template || !GetConstraintGizmoTransform(ParentFrame) ||
		!FPhysicsDebugDraw().GetConstraintFrames(MeshComponent, Template->DefaultInstance, ParentFrameWorld, ChildFrameWorld))
	{
		return false;
	}

	const int32 ParentBone = FindBoneIndexByName(Template->DefaultInstance.ConstraintBone2);
	const int32 ChildBone = FindBoneIndexByName(Template->DefaultInstance.ConstraintBone1);
	if (ParentBone == INDEX_NONE || ChildBone == INDEX_NONE)
	{
		return false;
	}

	const FMatrix ParentBoneWorld = MeshComponent->GetBoneWorldMatrix(ParentBone);
	const FMatrix ChildBoneWorld = MeshComponent->GetBoneWorldMatrix(ChildBone);
	if (fabsf(ParentBoneWorld.Determinant()) < 1e-12f || fabsf(ChildBoneWorld.Determinant()) < 1e-12f)
	{
		return false;
	}

	DragStartConstraint = Template->DefaultInstance;
	DragBoneWorldInverse = ParentBoneWorld.Inverse();
	DragChildBoneWorldInverse = ChildBoneWorld.Inverse();
	DragChildFrameRelative = ChildFrameWorld * ParentFrame.GetLocalMatrix().Inverse();
	return true;
}

bool FPhysicsAssetEditor::UpdatePhysicsGizmo()
{
	if (!CanProcessViewportInput() || !PhysicsGizmo.GetTarget())
	{
		PhysicsGizmo.EndDrag();
		return false;
	}

	const bool bConstraint = SelectionType == EPhysicsAssetSelection::Constraint;
	if (PointerInput.bCancel)
	{
		if (PhysicsGizmo.IsUsing())
		{
			if (UPhysicsConstraintTemplate* Template = bConstraint ? GetSelectedConstraintTemplate() : nullptr)
			{
				Template->DefaultInstance = DragStartConstraint;
			}
			else if (USkeletalBodySetup* BodySetup = GetSelectedBodySetup())
			{
				BodySetup->AggGeom = DragStartGeom;
			}
		}
		PhysicsGizmo.EndDrag();
		return false;
	}

	const bool bWasUsing = PhysicsGizmo.IsUsing();
	if ((!PointerInput.bHovered && !bWasUsing) || CameraInput.bCaptured)
	{
		PhysicsGizmo.EndDrag();
		return false;
	}

	SyncPhysicsGizmo();
	const int32 Width = static_cast<int32>(ViewportClient->GetWidth());
	const int32 Height = static_cast<int32>(ViewportClient->GetHeight());
	const FVector2 MousePosition(PointerInput.MouseUV.X * Width, PointerInput.MouseUV.Y * Height);
	const FRay MouseRay = ViewportClient->DeprojectScreenToWorld(MousePosition);

	const bool bUpdateDrag = bWasUsing || (PointerInput.bMousePressed && PointerInput.bMouseDown);
	PhysicsGizmo.Update(MouseRay, MousePosition, ViewportClient->GetViewProjectionMatrix(),
		Width, Height, bUpdateDrag, ViewportClient->GetViewLocation(), !ViewportClient->IsPerspective());

	if (!bWasUsing && PhysicsGizmo.IsUsing() && !(bConstraint ? BeginConstraintDrag() : BeginShapeDrag()))
	{
		PhysicsGizmo.EndDrag();
		return true;
	}

	if (PhysicsGizmo.IsUsing())
	{
		bConstraint ? ApplyConstraintTransform() : ApplyShapeTransform();
		if (!PointerInput.bMouseDown)
		{
			PhysicsGizmo.EndDrag();
		}
	}

	return bWasUsing || PhysicsGizmo.IsUsing() || PhysicsGizmo.GetHoveredAxis() >= 0;
}

void FPhysicsAssetEditor::ApplyConstraintTransform()
{
	UPhysicsConstraintTemplate* Template = GetSelectedConstraintTemplate();
	if (!Template)
	{
		return;
	}

	// 부모 프레임을 기즈모로 옮기고, 자식 프레임은 드래그 시작 때의 상대 위치를 유지한다
	const FMatrix ParentFrameWorld = GizmoTarget->GetWorldMatrix();
	const FMatrix ChildFrameWorld = DragChildFrameRelative * ParentFrameWorld;
	SetRefFrame(Template->DefaultInstance, 1, ParentFrameWorld * DragBoneWorldInverse);
	SetRefFrame(Template->DefaultInstance, 0, ChildFrameWorld * DragChildBoneWorldInverse);
	MarkDirty();
}

void FPhysicsAssetEditor::ApplyShapeTransform()
{
	USkeletalBodySetup* BodySetup = GetSelectedBodySetup();
	if (!BodySetup)
	{
		return;
	}

	// 기즈모 배율은 드래그 시작부터 누적이므로 매번 시작 값에서 다시 계산한다
	FKAggregateGeom& Geom = BodySetup->AggGeom;
	Geom = DragStartGeom;

	const UE::Widget::EWidgetMode Mode = ModeTools.GetWidgetMode();
	const FVector Center = DragBoneWorldInverse.TransformPosition(GizmoTarget->GetWorldLocation());
	const FRotator Rotation = (DragBoneWorldRotation.Inverse() *
		GizmoTarget->GetRelativeRotation().Quaternion()).Normalized().ToFRotator();
	const FVector Scale = GizmoTarget->GetRelativeScale3D();

	if (!Geom.SphylElems.IsEmpty())
	{
		FKSphylElem& Elem = Geom.SphylElems[0];
		if (Mode == UE::Widget::WM_Translate)
		{
			Elem.Center = Center;
		}
		else if (Mode == UE::Widget::WM_Rotate)
		{
			Elem.Rotation = Rotation;
		}
		else if (Mode == UE::Widget::WM_Scale)
		{
			const float TotalHeight = (Elem.Length + Elem.Radius * 2.0f) * Scale.Z;
			Elem.Radius = std::max(Elem.Radius * DominantScale(Scale.X, Scale.Y), MinShapeSize);
			Elem.Length = std::max(TotalHeight - Elem.Radius * 2.0f, 0.0f);
		}
	}
	else if (!Geom.BoxElems.IsEmpty())
	{
		FKBoxElem& Elem = Geom.BoxElems[0];
		if (Mode == UE::Widget::WM_Translate)
		{
			Elem.Center = Center;
		}
		else if (Mode == UE::Widget::WM_Rotate)
		{
			Elem.Rotation = Rotation;
		}
		else if (Mode == UE::Widget::WM_Scale)
		{
			Elem.X = std::max(Elem.X * Scale.X, MinShapeSize);
			Elem.Y = std::max(Elem.Y * Scale.Y, MinShapeSize);
			Elem.Z = std::max(Elem.Z * Scale.Z, MinShapeSize);
		}
	}
	else if (!Geom.SphereElems.IsEmpty())
	{
		FKSphereElem& Elem = Geom.SphereElems[0];
		if (Mode == UE::Widget::WM_Translate)
		{
			Elem.Center = Center;
		}
		else if (Mode == UE::Widget::WM_Scale)
		{
			const float SphereScale = DominantScale(DominantScale(Scale.X, Scale.Y), Scale.Z);
			Elem.Radius = std::max(Elem.Radius * SphereScale, MinShapeSize);
		}
	}

	MarkDirty();
}

bool FPhysicsAssetEditor::PickPhysics(const FRay& Ray, EPhysicsAssetSelection& OutType, int32& OutIndex) const
{
	FPhysicsDebugDraw DebugDraw;
	float NearestT = FLT_MAX;

	if (bShowConstraints)
	{
		const float Radius = DebugDraw.GetConstraintDrawSize(MeshComponent, ConstraintDrawSize);
		for (int32 i = 0; i < PhysicsAsset->ConstraintSetup.Num(); ++i)
		{
			FMatrix ParentFrameWorld, ChildFrameWorld;
			float T = 0.0f;
			if (PhysicsAsset->ConstraintSetup[i] &&
				DebugDraw.GetConstraintFrames(MeshComponent, PhysicsAsset->ConstraintSetup[i]->DefaultInstance, ParentFrameWorld, ChildFrameWorld) &&
				RayIntersectsSphere(Ray, ParentFrameWorld.TransformPosition(FVector::ZeroVector), Radius, T) && T < NearestT)
			{
				NearestT = T;
				OutType = EPhysicsAssetSelection::Constraint;
				OutIndex = i;
			}
		}
		// 관절 구는 Body 캡슐 안쪽에 묻히므로 맞았으면 Body보다 우선한다
		if (OutType == EPhysicsAssetSelection::Constraint)
		{
			return true;
		}
	}

	if (!bShowBodies)
	{
		return false;
	}

	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset->SkeletalBodySetups.Num(); ++BodyIndex)
	{
		const USkeletalBodySetup* BodySetup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
		FTransform BoneWorld;
		if (!BodySetup || !DebugDraw.GetBoneWorldTransform(MeshComponent, BodySetup->BoneName.ToString(), BoneWorld))
		{
			continue;
		}

		const FMatrix BodyMatrix = BoneWorld.GetLocalMatrix();
		const FVector& Scale = BoneWorld.Scale;
		const auto Consider = [&](bool bHit, float T)
		{
			if (bHit && T < NearestT)
			{
				NearestT = T;
				OutType = EPhysicsAssetSelection::Body;
				OutIndex = BodyIndex;
			}
		};

		for (const FKSphereElem& Elem : BodySetup->AggGeom.SphereElems)
		{
			const float Radius = Elem.Radius * std::max({ std::abs(Scale.X), std::abs(Scale.Y), std::abs(Scale.Z) });
			float T = 0.0f;
			Consider(RayIntersectsSphere(Ray, BodyMatrix.TransformPosition(Elem.Center), Radius, T), T);
		}

		for (const FKSphylElem& Elem : BodySetup->AggGeom.SphylElems)
		{
			const FMatrix ElemWorld = DebugDraw.MakeElemMatrix(Elem.Center, Elem.Rotation) * BodyMatrix;
			const FVector Center = ElemWorld.TransformPosition(FVector::ZeroVector);
			const FVector Up = FVector(ElemWorld.M[2][0], ElemWorld.M[2][1], ElemWorld.M[2][2]).GetSafeNormal();
			const float Radius = Elem.Radius * std::max(std::abs(Scale.X), std::abs(Scale.Y));
			const float HalfLength = Elem.Length * 0.5f * std::abs(Scale.Z);
			float T = 0.0f;
			Consider(RayIntersectsCapsule(Ray, Center - Up * HalfLength, Center + Up * HalfLength, Radius, T), T);
		}

		for (const FKBoxElem& Elem : BodySetup->AggGeom.BoxElems)
		{
			const FMatrix ElemWorld = DebugDraw.MakeElemMatrix(Elem.Center, Elem.Rotation) * BodyMatrix;
			const FVector HalfExtent(Elem.X * 0.5f, Elem.Y * 0.5f, Elem.Z * 0.5f);
			float T = 0.0f;
			Consider(RayIntersectsAABB(ToLocalRay(Ray, ElemWorld), HalfExtent * -1.0f, HalfExtent, T), T);
		}
	}

	return OutType != EPhysicsAssetSelection::None;
}
