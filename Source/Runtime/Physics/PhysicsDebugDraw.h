#pragma once

#include "Math/Transform.h"

class FLineBatcher;
class UWorld;
class AActor;
class UStaticMeshComponent;
class USkeletalMeshComponent;
struct FKAggregateGeom;
struct FKSphereElem;
struct FKBoxElem;
struct FKSphylElem;
struct FKConvexElem;
struct FConstraintInstance;


class FPhysicsDebugDraw
{
public:
	// 바디 하나의 Shape 전부를 BodyWorld 위치에 그린다
	void DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& Geom,
		const FTransform& BodyWorld, const FVector4& Color);
	// 월드의 StaticMesh, SkeletalMesh 물리 바디를 전부 그린다. Selected 액터의 바디는 강조색
	// Shape 컴포넌트(Box/Sphere/Capsule)는 DrawCollisionShapes가 그리므로 제외한다
	void DrawPhysicsBodies(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);
	//조인트 하나의 회전 제한(Swing 원뿔 + Twist 호)을 그린다
	void DrawConstraintLimit(FLineBatcher* LineBatcher, const FConstraintInstance& Constraint,
		const FMatrix& JointWorld, bool bSelected) const;
	// 월드의 SkeletalMesh PhysicsAsset 조인트 제한을 전부 그린다. Selected 액터의 조인트는 강조색
	void DrawConstraintLimits(UWorld* World, FLineBatcher* LineBatcher, const AActor* Selected);

private:
	// 바디 기본색(하늘색)과 선택된 액터의 강조색(주황)
	const FVector4 BodyColor = FVector4(0.3f, 0.8f, 1.0f, 1.0f);
	const FVector4 SelectedBodyColor = FVector4(1.0f, 0.6f, 0.1f, 1.0f);

	// 조인트 제한 색: Swing 원뿔(초록), Twist 호(빨강), 선택 시 둘 다 강조색
	const FVector4 SwingColor = FVector4(0.3f, 1.0f, 0.3f, 1.0f);
	const FVector4 TwistColor = FVector4(1.0f, 0.3f, 0.3f, 1.0f);
	const FVector4 SelectedConstraintColor = FVector4(1.0f, 0.9f, 0.2f, 1.0f);
	// 원뿔 길이 = Twist 호 반지름
	const float ConstraintDrawSize = 0.25f;

	// 메시 에셋의 BodySetup 모양. B의 UBodySetup이 들어오기 전까지는 nullptr
	const FKAggregateGeom* FindStaticMeshGeom(const UStaticMeshComponent* Component) const;
	
	void DrawSphereElem(FLineBatcher* LineBatcher, const FKSphereElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
	void DrawBoxElem(FLineBatcher* LineBatcher, const FKBoxElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
	void DrawSphylElem(FLineBatcher* LineBatcher, const FKSphylElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
	void DrawConvexElem(FLineBatcher* LineBatcher, const FKConvexElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
	
	// 본 이름 → 본 월드 트랜스폼 (본 컴포넌트 공간 × 컴포넌트 월드). 본이 없으면 false
	bool			GetBoneWorldTransform(const USkeletalMeshComponent* Component, const FString& BoneName, FTransform& OutBoneWorld) const;
	bool			ShouldDrawComponent(const UPrimitiveComponent* Component, const UWorld* World);
	FMatrix			MakeElemMatrix(const FVector& Center, const FRotator& Rotation) const;
	const FVector4&	PickBodyColor(const UPrimitiveComponent* Component, const AActor* Selected);
};


