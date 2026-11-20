#pragma once

#include "EnginePCH.h"

#include "Render/LineBatcher.h"

#include "PhysicsDebugDraw.h"
#include "AggregateGeom.h"


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

void FPhysicsDebugDraw::DrawSphereElem(FLineBatcher* LineBatcher, const FKSphereElem& SphereElem, const FTransform& BodyWorld, const FVector4& Color) const
{
	FMatrix	BodyMatrix = BodyWorld.GetLocalMatrix();
	FVector	WorldCenter = BodyMatrix.TransformPosition(SphereElem.Center);
	float MaxScale = std::max({ std::abs(BodyWorld.Scale.X), std::abs(BodyWorld.Scale.Y), std::abs(BodyWorld.Scale.Z) });
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

	if (Vertices.Num() < 3 || Vertices.Num() % 3 != 0)
	{
		return;
	}

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
