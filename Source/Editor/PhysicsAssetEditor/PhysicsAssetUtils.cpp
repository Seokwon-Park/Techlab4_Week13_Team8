#include "EnginePCH.h"
#include "PhysicsAssetUtils.h"
#include "Physics/PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

namespace
{
	constexpr float CapsuleRadiusRatio = 0.2f;
	// UE Min Bone Size에 해당 (메시 대각선 대비)
	constexpr float MinBoneSizeRatio = 0.02f;

	// X × Y = Dir
	void MakeBasisFromZ(const FVector& Dir, FVector& OutX, FVector& OutY)
	{
		const FVector Helper = std::abs(Dir.Z) < 0.99f ? FVector(0, 0, 1) : FVector(1, 0, 0);
		OutX = Helper.Cross(Dir).GetSafeNormal();
		OutY = Dir.Cross(OutX);
	}

	FRotator MakeRotationFromZ(const FVector& Dir)
	{
		FVector X, Y;
		MakeBasisFromZ(Dir, X, Y);

		FMatrix M = FMatrix::Identity;
		M.SetAxis(0, X);
		M.SetAxis(1, Y);
		M.SetAxis(2, Dir);
		return MatrixToRotator(M);
	}
}

bool FPhysicsAssetUtils::CreateFromSkeletalMesh(UPhysicsAsset& PhysicsAsset, const USkeletalMesh& Mesh)
{
	const TArray<FBoneInfo>& Bones = Mesh.GetRefSkeleton();
	const int32 NumBones = Bones.Num();
	if (NumBones == 0)
	{
		return false;
	}

	// 부모가 항상 앞 인덱스라 한 번에 쌓을 수 있다
	TArray<FMatrix> ComponentBind;
	ComponentBind.SetNum(NumBones);
	TArray<int32> FirstChild;
	FirstChild.SetNum(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		const int32 Parent = Bones[i].ParentIndex;
		const FMatrix Local = Bones[i].LocalBindPose.GetLocalMatrix();
		ComponentBind[i] = Parent >= 0 ? Local * ComponentBind[Parent] : Local;
		FirstChild[i] = INDEX_NONE;
		if (Parent >= 0 && FirstChild[Parent] == INDEX_NONE)
		{
			FirstChild[Parent] = i;
		}
	}

	float MinBoneSize = 0.0f;
	const auto& LODs = Mesh.GetRenderData().LODRenderData;
	if (!LODs.IsEmpty())
	{
		const FBox& Box = LODs[0].BindAABB;
		MinBoneSize = (Box.Max - Box.Min).Size() * MinBoneSizeRatio;
	}

	const auto GetComponentPosition = [&](int32 BoneIndex)
	{
		return ComponentBind[BoneIndex].TransformPosition(FVector::ZeroVector);
	};

	// BodyAxis: 본 로컬 공간의 바디 축 = 조인트 Twist 축
	TArray<int32> BoneToBody;
	BoneToBody.SetNum(NumBones);
	TArray<FVector> BodyAxis;
	BodyAxis.SetNum(NumBones);

	for (int32 i = 0; i < NumBones; ++i)
	{
		BoneToBody[i] = INDEX_NONE;

		// 캡슐 구간(컴포넌트 공간): 첫 자식까지. 자식이 없으면 부모에서 온 방향으로 그 절반 길이
		const FVector Start = GetComponentPosition(i);
		FVector End;
		if (FirstChild[i] != INDEX_NONE)
		{
			End = GetComponentPosition(FirstChild[i]);
		}
		else if (Bones[i].ParentIndex >= 0)
		{
			End = Start + (Start - GetComponentPosition(Bones[i].ParentIndex)) * 0.5f;
		}
		else
		{
			continue;
		}

		if ((End - Start).Size() < MinBoneSize)
		{
			continue;
		}

		const FMatrix InverseBone = ComponentBind[i].Inverse();
		const FVector Segment = InverseBone.TransformPosition(End);
		const float Length = Segment.Size();
		if (Length < SMALL_NUMBER)
		{
			continue;
		}
		const FVector Dir = Segment / Length;

		FKSphylElem Capsule;
		Capsule.Radius = Length * CapsuleRadiusRatio;
		Capsule.Length = std::max(Length - Capsule.Radius * 2.0f, 0.0f);
		Capsule.Center = Segment * 0.5f;
		Capsule.Rotation = MakeRotationFromZ(Dir);

		USkeletalBodySetup* BodySetup = NewObject<USkeletalBodySetup>(&PhysicsAsset);
		BodySetup->BoneName = FName(Bones[i].Name);
		BodySetup->AggGeom.SphylElems.Add(Capsule);

		BoneToBody[i] = PhysicsAsset.SkeletalBodySetups.Num();
		BodyAxis[i] = Dir;
		PhysicsAsset.SkeletalBodySetups.Add(BodySetup);
	}

	if (PhysicsAsset.SkeletalBodySetups.IsEmpty())
	{
		return false;
	}

	// 조인트 위치는 자식 본의 원점
	for (int32 Child = 0; Child < NumBones; ++Child)
	{
		if (BoneToBody[Child] == INDEX_NONE)
		{
			continue;
		}

		int32 Parent = Bones[Child].ParentIndex;
		while (Parent >= 0 && BoneToBody[Parent] == INDEX_NONE)
		{
			Parent = Bones[Parent].ParentIndex;
		}
		if (Parent < 0)
		{
			continue;
		}

		UPhysicsConstraintTemplate* Template = NewObject<UPhysicsConstraintTemplate>(&PhysicsAsset);
		FConstraintInstance& Constraint = Template->DefaultInstance;
		Constraint.ConstraintBone1 = FName(Bones[Child].Name);
		Constraint.ConstraintBone2 = FName(Bones[Parent].Name);

		// Frame1(자식 본 공간): X = 바디 축(Twist), Y = 바디 축에 수직인 아무 축
		FVector SecAxis, Unused;
		MakeBasisFromZ(BodyAxis[Child], SecAxis, Unused);
		Constraint.Pos1 = FVector::ZeroVector;
		Constraint.PriAxis1 = BodyAxis[Child];
		Constraint.SecAxis1 = SecAxis;

		// Frame2(부모 본 공간): 같은 프레임을 바인드 포즈에서 부모 본 기준으로 옮긴다
		const FMatrix ChildToParent = ComponentBind[Child] * ComponentBind[Parent].Inverse();
		const FVector Pri2 = ChildToParent.TransformVector(Constraint.PriAxis1).GetSafeNormal();
		FVector Sec2 = ChildToParent.TransformVector(Constraint.SecAxis1);
		Sec2 = (Sec2 - Pri2 * Sec2.Dot(Pri2)).GetSafeNormal();
		Constraint.Pos2 = ChildToParent.TransformPosition(FVector::ZeroVector);
		Constraint.PriAxis2 = Pri2;
		Constraint.SecAxis2 = Sec2;

		PhysicsAsset.ConstraintSetup.Add(Template);
		if (Constraint.ProfileInstance.bDisableCollision)
		{
			PhysicsAsset.DisableCollision(BoneToBody[Child], BoneToBody[Parent]);
		}
	}

	PhysicsAsset.UpdateBodySetupIndexMap();
	return true;
}
