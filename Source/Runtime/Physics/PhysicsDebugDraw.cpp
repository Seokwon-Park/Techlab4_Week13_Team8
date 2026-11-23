#include "EnginePCH.h"

#include "Render/LineBatcher.h"

#include "UObject/UObjectIterator.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

#include "PhysicsDebugDraw.h"
#include "AggregateGeom.h"
#include "ConstraintInstance.h"

namespace
{
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

		 //TODO(B): USkeletalMesh::PhysicsAsset, UBodySetup이 들어오면 구현
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
	const FMatrix& JointWorld, bool bSelected) const
{
	if (!LineBatcher)
	{
		return;
	}

	const FVector Origin(JointWorld.M[3][0], JointWorld.M[3][1], JointWorld.M[3][2]);
	const FVector AxisX = FVector(JointWorld.M[0][0], JointWorld.M[0][1], JointWorld.M[0][2]).GetSafeNormal();
	const FVector AxisY = FVector(JointWorld.M[1][0], JointWorld.M[1][1], JointWorld.M[1][2]).GetSafeNormal();

	const FConeConstraint& Cone = Constraint.ProfileInstance.ConeLimit;
	const FTwistConstraint& Twist = Constraint.ProfileInstance.TwistLimit;
	const float Swing1Radian = GetLimitRadian(Cone.Swing1LimitDegrees, Cone.Swing1Motion);
	const float Swing2Radian = GetLimitRadian(Cone.Swing2LimitDegrees, Cone.Swing2Motion);
	const float TwistRadian = GetLimitRadian(Twist.TwistLimitDegrees, Twist.TwistMotion);

	const FVector4& ConeColor = bSelected ? SelectedConstraintColor : SwingColor;
	const FVector4& ArcColor = bSelected ? SelectedConstraintColor : TwistColor;

	// Swing: 둘 다 Locked면 안 그림, 둘 다 Free면 구, 그 외 원뿔
	const bool bLockAllSwing = Cone.Swing1Motion == ACM_Locked && Cone.Swing2Motion == ACM_Locked;
	const bool bFreeAllSwing = Cone.Swing1Motion == ACM_Free && Cone.Swing2Motion == ACM_Free;
	if (!bLockAllSwing)
	{
		if (bFreeAllSwing)
		{
			LineBatcher->AddSphere(Origin, ConstraintDrawSize * 0.9f, ConeColor);
		}
		else
		{
			LineBatcher->AddSwingCone(Origin, AxisX, AxisY, Swing1Radian, Swing2Radian, ConstraintDrawSize, ConeColor);
		}
	}

	// Twist: Locked면 안 그림, Free면 원 전체(PI), Limited면 ±각도 부채꼴
	if (Twist.TwistMotion != ACM_Locked)
	{
		LineBatcher->AddTwistArc(Origin, AxisX, AxisY, TwistRadian, ConstraintDrawSize, ArcColor);
	}
}

void FPhysicsDebugDraw::DrawConstraintLimits(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected)
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

		// TODO(B): USkeletalMesh::PhysicsAsset이 들어오면 구현
	}
}
