#include "EnginePCH.h"
#include "LineBatcher.h"
#include "Components/PrimitiveComponent.h"

#include "RenderCommand.h"
#include "RenderResourceManager.h"

#include "UObject/UObjectIterator.h"

FLineBatcher::~FLineBatcher()
{
	delete[] VertexBufferBase;
}

bool FLineBatcher::Init(FRenderer* InRenderer, UWorld* InWorld)
{
	Renderer = InRenderer;
	World = InWorld;

	LineShader = FRenderResourceManager::GetShaderProgram("Resources/Shader/LineShader.hlsl");

	PipelineState.Shader = LineShader;
	PipelineState.Topology = EPrimitiveTopology::LineList;

	MaxVertices = 65536;
	VertexBufferBase = new FLineBatchVertex[MaxVertices];

	VertexBuffer = RenderCommand::CreateDynamicVertexBuffer(sizeof(FLineBatchVertex) * MaxVertices, sizeof(FLineBatchVertex));

	CB = RenderCommand::CreateConstantBuffer(sizeof(FMatrix));

	return true;
}

void FLineBatcher::BuildVertexBuffer()
{
	for (TObjectIterator<UPrimitiveComponent> Itr; Itr; ++Itr)
	{
		if (Itr->GetWorld() == World)
		{
			// 로컬 AABB와 월드 행렬을 그대로 넘기고 변환은 AddOrientedBox가 한다.
			// (월드 행렬이어야 부모에 붙은 컴포넌트도 제자리에 그려진다)
			AddOrientedBox(
				Itr->CalcLocalBounds(),
				Itr->GetWorldMatrix(),
				FVector4(1.0f, 1.0f, 1.0f, 1.0f));
		}
	}
}

void FLineBatcher::BeginFrame()
{
	VertexBufferPtr = VertexBufferBase;
	VertexCount = 0;
}

void FLineBatcher::AddLine(const FVector& A, const FVector& B, const FVector4& Color)
{
	if (VertexCount + 2 > MaxVertices)
		return;   // 넘치면 버림

	*VertexBufferPtr++ = { A, Color };
	*VertexBufferPtr++ = { B, Color };

	VertexCount += 2;
}

namespace
{
	// 박스 모서리 12개. 코너 배열의 인덱스 쌍이며 AABB/OBB가 같은 순서를 쓴다.
	const int BOX_EDGES[12][2] = {
		{0,1},{1,2},{2,3},{3,0},      // 아랫면
		{4,5},{5,6},{6,7},{7,4},      // 윗면
		{0,4},{1,5},{2,6},{3,7},      // 기둥
	};

	// Min/Max로 코너 8개를 만든다. BOX_EDGES가 기대하는 순서다.
	void MakeCorners(const FVector& Min, const FVector& Max, FVector* OutCorners)
	{
		OutCorners[0] = { Min.X, Min.Y, Min.Z };
		OutCorners[1] = { Max.X, Min.Y, Min.Z };
		OutCorners[2] = { Max.X, Max.Y, Min.Z };
		OutCorners[3] = { Min.X, Max.Y, Min.Z };
		OutCorners[4] = { Min.X, Min.Y, Max.Z };
		OutCorners[5] = { Max.X, Min.Y, Max.Z };
		OutCorners[6] = { Max.X, Max.Y, Max.Z };
		OutCorners[7] = { Min.X, Max.Y, Max.Z };
	}
}

void FLineBatcher::AddBox(FBox BoundingBox, FVector4 Color)
{
	FVector C[8];
	MakeCorners(BoundingBox.Min, BoundingBox.Max, C);

	for (const auto& Edge : BOX_EDGES)
	{
		AddLine(C[Edge[0]], C[Edge[1]], Color);
	}
}

void FLineBatcher::AddOrientedBox(const FBox& LocalBox, const FMatrix& WorldMatrix, const FVector4& Color)
{
	FVector C[8];
	MakeCorners(LocalBox.Min, LocalBox.Max, C);

	// 변환 후에 Min/Max를 다시 재면 회전이 뭉개지므로,
	// 코너를 하나씩 옮긴 뒤 원래 순서 그대로 잇는다.
	for (FVector& Corner : C)
	{
		const FVector4 WorldCorner = FVector4(Corner, 1.0f) * WorldMatrix;
		Corner = FVector(WorldCorner.X, WorldCorner.Y, WorldCorner.Z);
	}

	for (const auto& Edge : BOX_EDGES)
	{
		AddLine(C[Edge[0]], C[Edge[1]], Color);
	}

}
void FLineBatcher::AddPath(const TArray<FVector>& Points, const FVector4& Color)
{
	if (Points.IsEmpty())
	{
		return;
	}
	for (size_t i = 0; i < Points.size() - 1; ++i) {
		AddLine(Points[i], Points[i + 1], Color);
	}
}

void FLineBatcher::AddPyramid(const FVector& BaseCenter, const FVector& Tip, float HalfWidth, const FVector4& Color)
{
	const FVector Offset = Tip - BaseCenter;
	if (Offset.SizeSquared() < 0.00000001f || HalfWidth <= 0.0f)
	{
		return;
	}

	// 축이 수직인 경우에도 외적이 영벡터가 되지 않도록 기준 축을 고릅니다.
	const FVector Direction = Offset.Normalized();
	const FVector Reference = fabsf(Direction.Z) < 0.99f ? FVector(0.0f, 0.0f, 1.0f) : FVector(0.0f, 1.0f, 0.0f);
	const FVector Right = Direction.Cross(Reference).Normalized() * HalfWidth;
	const FVector Up = Direction.Cross(Right).Normalized() * HalfWidth;
	const FVector Corners[] = {
		BaseCenter + Right + Up, BaseCenter - Right + Up,
		BaseCenter - Right - Up, BaseCenter + Right - Up
	};

	for (int32 Index = 0; Index < 4; ++Index)
	{
		AddLine(Corners[Index], Corners[(Index + 1) % 4], Color);
		AddLine(Corners[Index], Tip, Color);
	}
}

void FLineBatcher::AddSphere(const FVector& Center, float Radius, const FVector4& Color, int32 Segments)
{
	if (Radius <= 0.0f || Segments < 3)
	{
		return;
	}

	for (int32 Index = 0; Index < Segments; ++Index)
	{
		const float AngleA = 2.0f * PI * static_cast<float>(Index) / Segments;
		const float AngleB = 2.0f * PI * static_cast<float>(Index + 1) / Segments;
		const float CosA = cosf(AngleA) * Radius;
		const float SinA = sinf(AngleA) * Radius;
		const float CosB = cosf(AngleB) * Radius;
		const float SinB = sinf(AngleB) * Radius;

		AddLine(Center + FVector(CosA, SinA, 0.0f), Center + FVector(CosB, SinB, 0.0f), Color);
		AddLine(Center + FVector(CosA, 0.0f, SinA), Center + FVector(CosB, 0.0f, SinB), Color);
		AddLine(Center + FVector(0.0f, CosA, SinA), Center + FVector(0.0f, CosB, SinB), Color);
	}
}

void FLineBatcher::AddCapsule(const FVector& Center, const FVector& Up, const FVector& Right,
	float Radius, float HalfHeight, const FVector4& Color, int32 Segments)
{
	constexpr float Pi = PI;

	const float Segment = std::max(HalfHeight - Radius, 0.0f);
	const FVector CapsuleA = Center + Up * Segment;
	const FVector CapsuleB = Center - Up * Segment;
	const FVector Y = Up.Cross(Right).GetSafeNormal();
	

	// 위·아래 고리
	AddWireArc(CapsuleA, Right, Y, Radius, 0, 2 * Pi, Color, Segments);
	AddWireArc(CapsuleB, Right, Y, Radius, 0, 2 * Pi, Color, Segments);
	// 위 반구 , 아래 반구
	AddWireArc(CapsuleA, Right, Up, Radius, 0, Pi, Color, Segments);
	AddWireArc(CapsuleA, Y, Up, Radius, 0, Pi, Color, Segments);
	AddWireArc(CapsuleB, Right, Up, Radius, 0, -Pi, Color, Segments);
	AddWireArc(CapsuleB, Y, Up, Radius, 0, -Pi, Color, Segments);
	// 옆선 4개
	for (const FVector& Side : { Right, Right * -1.0f, Y, Y * -1.0f })
		AddLine(CapsuleA + Side * Radius, CapsuleB + Side * Radius, Color);
}

void FLineBatcher::AddSwingCone(const FVector& Apex, const FVector& Axis, const FVector& Right,
	float Swing1Rad, float Swing2Rad, float Length, const FVector4& Color, int32 Segments)
{
	if (Length <= 0.0f || Segments < 3)
	{
		return;
	}

	const FVector Up2 = Axis.Cross(Right).GetSafeNormal();

	// 두 각이 모두 0이면 축 방향 선 하나만 그린다.
	if (Swing1Rad <= 0.0f && Swing2Rad <= 0.0f)
	{
		AddLine(Apex, Apex + Axis * Length, Color);
		return;
	}

	// 고리 위의 점: Axis를 W 방향으로 |W|만큼 기울인 끝점
	auto PointAt = [&](float T)
	{
		const FVector W = Right * (Swing1Rad * std::cos(T)) + Up2 * (Swing2Rad * std::sin(T));
		const float Theta = W.Length();
		if (Theta < 1e-6f)
		{
			return Apex + Axis * Length;   // 한쪽 각이 0일 때 그 방향은 축 그대로
		}
		const FVector Dir = Axis * std::cos(Theta) + (W / Theta) * std::sin(Theta);
		return Apex + Dir * Length;
	};

	// 옆선은 4개 (Right, Up2, -Right, -Up2 방향)
	const int32 SideStep = std::max(Segments / 4, 1);

	FVector Prev = PointAt(0.0f);
	AddLine(Apex, Prev, Color);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const FVector P = PointAt(2.0f * PI * i / Segments);
		AddLine(Prev, P, Color);
		if (i % SideStep == 0 && i != Segments)
		{
			AddLine(Apex, P, Color);
		}
		Prev = P;
	}
}

void FLineBatcher::AddConvex(const TArray<FVector>& LocalVertices, const TArray<uint32>& Indices,
	const FMatrix& WorldMatrix, const FVector4& Color)
{
	const uint32 VertexNum = static_cast<uint32>(LocalVertices.Num());
	if (VertexNum == 0 || Indices.Num() % 3 != 0)
	{
		return;
	}

	// 정점을 먼저 한 번씩만 월드로 옮긴다
	TArray<FVector> WorldVertices;
	WorldVertices.Reserve(VertexNum);
	for (const FVector& V : LocalVertices)
	{
		const FVector4 W = FVector4(V, 1.0f) * WorldMatrix;
		WorldVertices.Add(FVector(W.X, W.Y, W.Z));
	}

	// 이웃 삼각형이 공유하는 엣지는 한 번만 그린다.
	TSet<uint64> Drawn;
	auto DrawEdge = [&](uint32 A, uint32 B)
	{
		if (A >= VertexNum || B >= VertexNum)
		{
			return;
		}
		const uint64 Key = (static_cast<uint64>(std::min(A, B)) << 32) | std::max(A, B);
		if (Drawn.Add(Key))
		{
			AddLine(WorldVertices[A], WorldVertices[B], Color);
		}
	};

	for (int32 i = 0; i < Indices.Num(); i += 3)
	{
		DrawEdge(Indices[i], Indices[i + 1]);
		DrawEdge(Indices[i + 1], Indices[i + 2]);
		DrawEdge(Indices[i + 2], Indices[i]);
	}
}

void FLineBatcher::AddTwistArc(const FVector& Center, const FVector& Axis, const FVector& Ref,
	float LimitRad, float Radius, const FVector4& Color, int32 Segments)
{
	if (Radius <= 0.0f)
	{
		return;
	}

	const FVector Up2 = Axis.Cross(Ref).GetSafeNormal();

	AddLine(Center, Center + Ref * Radius, Color);

	if (LimitRad <= 0.0f)
	{
		return;
	}

	if (LimitRad >= PI)
	{
		AddWireArc(Center, Ref, Up2, Radius, -PI, PI, Color, Segments);
		return;
	}

	// 부채꼴: 호 + 양 끝 반지름 선
	AddWireArc(Center, Ref, Up2, Radius, -LimitRad, LimitRad, Color, Segments);
	const FVector EndPlus = Center + (Ref * std::cos(LimitRad) + Up2 * std::sin(LimitRad)) * Radius;
	const FVector EndMinus = Center + (Ref * std::cos(LimitRad) - Up2 * std::sin(LimitRad)) * Radius;
	AddLine(Center, EndPlus, Color);
	AddLine(Center, EndMinus, Color);
}


void FLineBatcher::AddWireArc(const FVector& Center, const FVector& AxisA, const FVector& AxisB,
	float Radius, float StartRad, float EndRad, const FVector4& Color, int32 Segments)
{
	auto PointAt = [&](float T) { return Center + AxisA * (std::cos(T) * Radius) + AxisB * (std::sin(T) * Radius); };
	FVector Prev = PointAt(StartRad);
	for (int32 i = 1; i <= Segments; ++i)
	{
		const FVector P = PointAt(StartRad + (EndRad - StartRad) * i / Segments);
		AddLine(Prev, P, Color);
		Prev = P;
	}
}


void FLineBatcher::Flush()
{
	RenderCommand::BindPipelineState(PipelineState);
	RenderCommand::BindVertexBuffer(VertexBuffer.get());
	RenderCommand::BindConstantBuffer(0, CB.get(), EShaderBindFlagBits::Vertex);

	RenderCommand::Draw(VertexCount, 0);
}

void FLineBatcher::OnRender(const FMatrix& ViewProjection)
{
	if (VertexCount == 0)
	{
		return;
	}

	FMatrix VP = ViewProjection;
	RenderCommand::UpdateBufferData(VertexBuffer.get(), VertexBufferBase, sizeof(FLineBatchVertex) * VertexCount);
	RenderCommand::UpdateBufferData(CB.get(), &VP, sizeof(FMatrix));

	Flush();
}

