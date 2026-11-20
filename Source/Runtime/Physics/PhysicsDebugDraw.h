#pragma once

#include "Math/Transform.h"

class FLineBatcher;
struct FKAggregateGeom;
struct FKSphereElem;
struct FKBoxElem;
struct FKSphylElem;
struct FKConvexElem;


class FPhysicsDebugDraw
{
public:
	// 바디 하나의 Shape 전부를 BodyWorld 위치에 그린다
	void DrawAggregateGeom(FLineBatcher* LineBatcher, const FKAggregateGeom& Geom,
		const FTransform& BodyWorld, const FVector4& Color);

private:
	FMatrix MakeElemMatrix(const FVector& Center, const FRotator& Rotation) const;

	void DrawSphereElem(FLineBatcher* LineBatcher, const FKSphereElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
	void DrawBoxElem(FLineBatcher* LineBatcher, const FKBoxElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
	void DrawSphylElem(FLineBatcher* LineBatcher, const FKSphylElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
	void DrawConvexElem(FLineBatcher* LineBatcher, const FKConvexElem& Elem, const FTransform& BodyWorld, const FVector4& Color) const;
};


