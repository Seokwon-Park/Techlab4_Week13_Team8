#include "EnginePCH.h"

#include "Render/LineBatcher.h"

#include "UObject/UObjectIterator.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "PhysicsDebugDraw.h"
#include "AggregateGeom.h"
#include "ConstraintInstance.h"
#include "PhysicsAsset.h"
#include "Render/SkeletalMesh.h"

namespace
{
	FVector GetMatrixOrigin(const FMatrix& M) { return FVector(M.M[3][0], M.M[3][1], M.M[3][2]); }
	FVector GetMatrixAxis(const FMatrix& M, int32 Row) { return FVector(M.M[Row][0], M.M[Row][1], M.M[Row][2]).GetSafeNormal(); }

	constexpr float JointThicknessRatio = 0.025f;

	void DrawArrow(FLineBatcher* LineBatcher, const FVector& Origin, const FVector& Direction, const FVector& Side,
		float Length, const FVector4& Color)
	{
		const FVector Tip = Origin + Direction * Length;
		LineBatcher->AddCylinder(Origin, Tip, Length * JointThicknessRatio, Color);
		const FVector HeadApex = Origin + Direction * (Length * 1.05f);
		LineBatcher->AddSwingCone(HeadApex, Direction * -1.0f, Side, PI * 0.25f, PI * 0.25f, Length * 0.1f, Color, 8, 1.0f);
	}

	float GetLimitRadian(float LimitDegrees, EAngularConstraintMotion Motion)
	{
		switch (Motion)
		{
		case ACM_Locked:
			return 0.0f;
		case ACM_Free:
			return PI;
		case ACM_Limited:
		default:
			return FMath::DegreesToRadians(std::clamp(LimitDegrees, 0.0f, 180.0f));
		}
	}
}


void FPhysicsDebugDraw::DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& Geom,
	const FTransform& BodyWorld, const FVector4& Color)
{
	if (!LineBatcher)
	{
		return;
	}

	for (const FKSphereElem& SphereElem : Geom.SphereElems)
	{
		DrawSphereElem(LineBatcher, SphereElem, BodyWorld, Color);
	}

	for (const FKBoxElem& BoxElem : Geom.BoxElems)
	{
		DrawBoxElem(LineBatcher, BoxElem, BodyWorld, Color);
	}

	for (const FKSphylElem& SphylElem : Geom.SphylElems)
	{
		DrawSphylElem(LineBatcher, SphylElem, BodyWorld, Color);
	}

	for (const FKConvexElem& ConvexElem : Geom.ConvexElems)
	{
		DrawConvexElem(LineBatcher, ConvexElem, BodyWorld, Color);
	}
}

FMatrix FPhysicsDebugDraw::MakeElemMatrix(const FVector& Center, const FRotator& Rotation) const
{
	// 스케일 1, 회전 Rotation, 위치 Center인 트랜스폼을 행렬로 바꾼다
	return FTransform(Rotation, Center).GetLocalMatrix();
}

bool FPhysicsDebugDraw::ShouldDrawComponent(const UPrimitiveComponent* Component, const UWorld* World)
{
	return Component && Component->GetWorld() == World && Component->IsRegistered();
}

const FVector4& FPhysicsDebugDraw::PickBodyColor(const UPrimitiveComponent* Component, const AActor* Selected)
{
	return (Selected && Component->GetOwner() == Selected) ? SelectedBodyColor : BodyColor;
}

void FPhysicsDebugDraw::DrawPhysicsBodies(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
{
	if (!World || !LineBatcher)
	{
		return;
	}

	// StaticMesh: 메시 전체가 바디 하나. 위치는 컴포넌트 월드 트랜스폼
	for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
	{
		UStaticMeshComponent* Component = *It;
		if (!ShouldDrawComponent(Component, World))
		{
			continue;
		}

		const FKAggregateGeom* Geom = FindStaticMeshGeom(Component);
		if (!Geom)
		{
			continue;
		}

		const FTransform ComponentWorld = FTransform::FromMatrix(Component->GetWorldMatrix());
		DrawAggregateGeom(LineBatcher, *Geom, ComponentWorld, PickBodyColor(Component, Selected));
	}

	// SkeletalMesh: 본마다 바디 하나. 위치는 본 월드 트랜스폼
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Component = *It;
		if (!ShouldDrawComponent(Component, World))
		{
			continue;
		}

		if (const UPhysicsAsset* PhysicsAsset = FindPhysicsAsset(Component))
		{
			DrawPhysicsAssetBodies(LineBatcher, Component, *PhysicsAsset, Selected && Component->GetOwner() == Selected);
		}
	}
}

void FPhysicsDebugDraw::DrawPhysicsAssetBodies(FLineBatcher* LineBatcher, const USkeletalMeshComponent* Component,
	const UPhysicsAsset& PhysicsAsset, bool bSelected, int32 SelectedBody)
{
	if (!LineBatcher || !Component)
	{
		return;
	}

	for (int32 BodyIndex = 0; BodyIndex < PhysicsAsset.SkeletalBodySetups.Num(); ++BodyIndex)
	{
		const USkeletalBodySetup* BodySetup = PhysicsAsset.SkeletalBodySetups[BodyIndex];
		FTransform BoneWorld;
		if (!BodySetup || !GetBoneWorldTransform(Component, BodySetup->BoneName.ToString(), BoneWorld))
		{
			continue;
		}

		const bool bHighlight = bSelected || BodyIndex == SelectedBody;
		DrawAggregateGeom(LineBatcher, BodySetup->AggGeom, BoneWorld, bHighlight ? SelectedBodyColor : BodyColor);
	}
}

void FPhysicsDebugDraw::DrawPhysicsAssetConstraints(FLineBatcher* LineBatcher, const USkeletalMeshComponent* Component,
	const UPhysicsAsset& PhysicsAsset, int32 SelectedConstraint, float ConstraintDrawSize)
{
	const USkeletalMesh* Mesh = Component ? Component->GetSkeletalMesh() : nullptr;
	if (!LineBatcher || !Mesh || Mesh->GetRenderData().LODRenderData.IsEmpty())
	{
		return;
	}

	const FBox& BindBox = Mesh->GetRenderData().LODRenderData[0].BindAABB;
	const FVector ComponentScale = FTransform::FromMatrix(Component->GetWorldMatrix()).Scale;
	const float MaxScale = std::max({ std::abs(ComponentScale.X), std::abs(ComponentScale.Y), std::abs(ComponentScale.Z) });
	const float DrawSize = (BindBox.Max - BindBox.Min).Size() * MaxScale * ConstraintSizeRatio * ConstraintDrawSize;

	for (int32 ConstraintIndex = 0; ConstraintIndex < PhysicsAsset.ConstraintSetup.Num(); ++ConstraintIndex)
	{
		const UPhysicsConstraintTemplate* Template = PhysicsAsset.ConstraintSetup[ConstraintIndex];
		if (!Template)
		{
			continue;
		}

		const FConstraintInstance& Constraint = Template->DefaultInstance;
		const int32 ParentBoneIndex = Component->FindBoneIndex(Constraint.ConstraintBone2.ToString());
		if (ParentBoneIndex < 0)
		{
			continue;
		}

		const FMatrix ParentFrameWorld = Constraint.GetRefFrame(1).GetLocalMatrix() * Component->GetBoneWorldMatrix(ParentBoneIndex);
		const int32 ChildBoneIndex = Component->FindBoneIndex(Constraint.ConstraintBone1.ToString());
		const FMatrix ChildFrameWorld = ChildBoneIndex >= 0
			? Constraint.GetRefFrame(0).GetLocalMatrix() * Component->GetBoneWorldMatrix(ChildBoneIndex)
			: ParentFrameWorld;
		DrawConstraintLimit(LineBatcher, Constraint, ParentFrameWorld, ChildFrameWorld,
			ConstraintIndex == SelectedConstraint ? DrawSize * SelectedConstraintSizeScale : DrawSize);
	}
}

const FKAggregateGeom* FPhysicsDebugDraw::FindStaticMeshGeom(const UStaticMeshComponent* Component) const
{
	// TODO(B): UStaticMesh::BodySetup이 들어오면 아래로 바꾼다
	// const UStaticMesh* Mesh = Component->GetStaticMesh();
	// return (Mesh && Mesh->BodySetup) ? &Mesh->BodySetup->AggGeom : nullptr;
	(void)Component;
	return nullptr;
}

const UPhysicsAsset* FPhysicsDebugDraw::FindPhysicsAsset(const USkeletalMeshComponent* Component) const
{
	const USkeletalMesh* Mesh = Component->GetSkeletalMesh();
	return Mesh ? Mesh->PhysicsAsset : nullptr;
}

bool FPhysicsDebugDraw::GetBoneWorldTransform(const USkeletalMeshComponent* Component, const FString& BoneName, FTransform& OutBoneWorld) const
{
	const int32 BoneIndex = Component->FindBoneIndex(BoneName);
	const TArray<FMatrix>& ComponentSpace = Component->GetComponentSpaceTransform();
	if (BoneIndex < 0 || BoneIndex >= ComponentSpace.Num())
	{
		return false;
	}

	const FMatrix BoneWorld = ComponentSpace[BoneIndex] * Component->GetWorldMatrix();
	OutBoneWorld = FTransform::FromMatrix(BoneWorld);
	return true;
}

void FPhysicsDebugDraw::DrawSphereElem(FLineBatcher* LineBatcher, const FKSphereElem& SphereElem, const FTransform& BodyWorld, const FVector4& Color) const
{
	FMatrix	BodyMatrix = BodyWorld.GetLocalMatrix();
	FVector	WorldCenter = BodyMatrix.TransformPosition(SphereElem.Center);
	float	MaxScale = std::max({ std::abs(BodyWorld.Scale.X), std::abs(BodyWorld.Scale.Y), std::abs(BodyWorld.Scale.Z) });
	float	WorldRadius = SphereElem.Radius * MaxScale;

	LineBatcher->AddSphere(WorldCenter, WorldRadius, Color);
}

void FPhysicsDebugDraw::DrawBoxElem(FLineBatcher* LineBatcher, const FKBoxElem& BoxElem, const FTransform& BodyWorld, const FVector4& Color) const
{
	FMatrix	BodyMatrix = BodyWorld.GetLocalMatrix();
	FMatrix ElemMatrix = MakeElemMatrix(BoxElem.Center, BoxElem.Rotation);
	FMatrix ElemWorld = ElemMatrix * BodyMatrix;
	FVector HalfExtent(BoxElem.X * 0.5f, BoxElem.Y * 0.5f, BoxElem.Z * 0.5f);
	FBox	LocalBox{ HalfExtent * -1.0f, HalfExtent };

	LineBatcher->AddOrientedBox(LocalBox, ElemWorld, Color);
}

void FPhysicsDebugDraw::DrawSphylElem(FLineBatcher* LineBatcher, const FKSphylElem& SphylElem, const FTransform& BodyWorld, const FVector4& Color) const
{
	const FMatrix BodyMatrix = BodyWorld.GetLocalMatrix();
	const FMatrix ElemWorld = MakeElemMatrix(SphylElem.Center, SphylElem.Rotation) * BodyMatrix;

	const FVector Center(ElemWorld.M[3][0], ElemWorld.M[3][1], ElemWorld.M[3][2]);
	const FVector Up = FVector(ElemWorld.M[2][0], ElemWorld.M[2][1], ElemWorld.M[2][2]).GetSafeNormal();  // 캡슐 축 (로컬 Z)
	const FVector Right = FVector(ElemWorld.M[0][0], ElemWorld.M[0][1], ElemWorld.M[0][2]).GetSafeNormal();  // 축에 수직 (로컬 X)

	const FVector& Scale = BodyWorld.Scale;
	const float RadiusScale = std::max(std::abs(Scale.X), std::abs(Scale.Y));
	const float LengthScale = std::abs(Scale.Z);

	const float WorldRadius = SphylElem.Radius * RadiusScale;
	const float HalfHeight = SphylElem.Length * 0.5f * LengthScale + WorldRadius;

	LineBatcher->AddCapsule(Center, Up, Right, WorldRadius, HalfHeight, Color);
}

void FPhysicsDebugDraw::DrawConvexElem(FLineBatcher* LineBatcher, const FKConvexElem& ConvexElem, const FTransform& BodyWorld, const FVector4& Color) const
{
	const TArray<FVector>& Vertices = ConvexElem.VertexData;
	const FMatrix BodyMatrix = BodyWorld.GetLocalMatrix();
	const FMatrix ElemWorld = ConvexElem.Transform.GetLocalMatrix() * BodyMatrix;

	TArray<uint32> Indices;
	Indices.Reserve(Vertices.Num());
	for (int32 i = 0; i < Vertices.Num(); ++i)
	{
		Indices.Add(static_cast<uint32>(i));
	}

	LineBatcher->AddConvex(Vertices, Indices, ElemWorld, Color);
}

void FPhysicsDebugDraw::DrawConstraintLimit(FLineBatcher* LineBatcher, const FConstraintInstance& Constraint,
	const FMatrix& ParentFrameWorld, const FMatrix& ChildFrameWorld, float DrawSize) const
{
	if (!LineBatcher)
	{
		return;
	}

	const FVector Origin = GetMatrixOrigin(ParentFrameWorld);
	const FVector AxisX = GetMatrixAxis(ParentFrameWorld, 0);
	const FVector AxisY = GetMatrixAxis(ParentFrameWorld, 1);
	const FVector ChildOrigin = GetMatrixOrigin(ChildFrameWorld);
	const FVector ChildAxisX = GetMatrixAxis(ChildFrameWorld, 0);
	const FVector ChildAxisY = GetMatrixAxis(ChildFrameWorld, 1);

	const FConeConstraint& Cone = Constraint.ProfileInstance.ConeLimit;
	const FTwistConstraint& Twist = Constraint.ProfileInstance.TwistLimit;
	const float Swing1Radian = GetLimitRadian(Cone.Swing1LimitDegrees, Cone.Swing1Motion);
	const float Swing2Radian = GetLimitRadian(Cone.Swing2LimitDegrees, Cone.Swing2Motion);
	const float TwistRadian = GetLimitRadian(Twist.TwistLimitDegrees, Twist.TwistMotion);


	// Swing: 둘 다 Locked면 안 그림, 둘 다 Free면 구, 그 외 원뿔
	const bool bLockAllSwing = Cone.Swing1Motion == ACM_Locked && Cone.Swing2Motion == ACM_Locked;
	const bool bFreeAllSwing = Cone.Swing1Motion == ACM_Free && Cone.Swing2Motion == ACM_Free;
	if (!bLockAllSwing)
	{
		if (bFreeAllSwing)
		{
			LineBatcher->AddSphere(Origin, DrawSize * 0.9f, SwingColor);
		}
		else
		{
			LineBatcher->AddSwingCone(Origin, AxisX, AxisY, Swing1Radian, Swing2Radian, DrawSize, SwingColor, 24, ConstraintFillAlpha);
		}

		DrawArrow(LineBatcher, Origin, ChildAxisX, ChildAxisY, DrawSize, SwingColor);
	}

	// Twist: Locked면 안 그림, Free면 원 전체(PI), Limited면 ±각도 부채꼴
	if (Twist.TwistMotion != ACM_Locked)
	{
		LineBatcher->AddTwistArc(Origin, AxisX, AxisY, TwistRadian, DrawSize, TwistColor, 24, ConstraintFillAlpha);

		const FVector TwistIndicator = (ChildAxisY - AxisX * ChildAxisY.Dot(AxisX)).GetSafeNormal();
		if (TwistIndicator.SizeSquared() > 0.0f)
		{
			DrawArrow(LineBatcher, Origin, TwistIndicator, AxisX, DrawSize, TwistColor);
		}
	}

	const float AxisLength = DrawSize * 0.1f;
	const float AxisThickness = DrawSize * JointThicknessRatio;
	const FVector4 AxisColors[3] = { FVector4(1, 0, 0, 1), FVector4(0, 1, 0, 1), FVector4(0, 0, 1, 1) };
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		LineBatcher->AddCylinder(ChildOrigin, ChildOrigin + GetMatrixAxis(ChildFrameWorld, Axis) * AxisLength, AxisThickness, AxisColors[Axis]);
		LineBatcher->AddCylinder(Origin, Origin + GetMatrixAxis(ParentFrameWorld, Axis) * AxisLength, AxisThickness, AxisColors[Axis]);
	}
}

void FPhysicsDebugDraw::DrawConstraintLimits(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher)
	{
		return;
	}

	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		USkeletalMeshComponent* Component = *It;
		if (!ShouldDrawComponent(Component, World))
		{
			continue;
		}

		if (const UPhysicsAsset* PhysicsAsset = FindPhysicsAsset(Component))
		{
			DrawPhysicsAssetConstraints(LineBatcher, Component, *PhysicsAsset);
		}
	}
}
