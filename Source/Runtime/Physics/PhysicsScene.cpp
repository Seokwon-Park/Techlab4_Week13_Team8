#include "EnginePCH.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSettings.h"
#include "Core/EngineLog.h"
#include "Physics/PhysXSDK.h"
#include "Collision/HitResult.h"
#include "Collision/Ray.h"
#include "Physics/CollisionQueryFilterCallback.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysXConversion.h"

#include <cmath>
#include <limits>
#include <geometry/PxGeometryQuery.h>
#include <algorithm>
#include <PxPhysics.h>
#include <PxScene.h>
#include <PxSceneDesc.h>
#include <extensions/PxDefaultSimulationFilterShader.h>

FPhysicsScene::FPhysicsScene()
{
	physx::PxPhysics* Physics = PhysXSDK::GetPhysics();
	physx::PxCpuDispatcher* Dispatcher = PhysXSDK::GetCpuDispatcher();
	if (!Physics || !Dispatcher)
	{
		return;
	}

	// 프로젝트 기본 중력과 PCM 설정의 씬 적용
	const FPhysicsSettings& Settings = FPhysicsSettings::Get();
	physx::PxSceneDesc Desc(Physics->getTolerancesScale());
	Desc.gravity = physx::PxVec3(0.0f, 0.0f, Settings.DefaultGravityZ);
	Desc.cpuDispatcher = Dispatcher;
	Desc.filterShader = physx::PxDefaultSimulationFilterShader;
	if (Settings.bEnablePCM)
	{
		Desc.flags |= physx::PxSceneFlag::eENABLE_PCM;
	}
	else
	{
		Desc.flags.clear(physx::PxSceneFlag::eENABLE_PCM);
	}
	Scene = Physics->createScene(Desc);
}

FPhysicsScene::~FPhysicsScene()
{
	// 월드가 소유한 네이티브 씬 해제
	if (Scene)
	{
		Scene->release();
	}
}

void FPhysicsScene::SetGravityZ(float GravityZ)
{
	// 유효 중력이 바뀐 경우의 네이티브 씬 갱신
	const physx::PxVec3 Gravity(0.0f, 0.0f, GravityZ);
	if (Scene && Scene->getGravity() != Gravity)
	{
		Scene->setGravity(Gravity);
	}
}

void FPhysicsScene::Tick(float DeltaSeconds)
{
	if (!Scene || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 프로젝트 고정 스텝 크기와 최대 횟수 조회
	const FPhysicsSettings& Settings = FPhysicsSettings::Get();
	const double FixedStepSeconds = Settings.FixedStepSeconds;
	const int32 MaxStepsPerTick = Settings.MaxStepsPerTick;
	const float StepDelta = static_cast<float>(FixedStepSeconds);
	AccumulatedSeconds = std::min(
		AccumulatedSeconds + static_cast<double>(DeltaSeconds),
		FixedStepSeconds * MaxStepsPerTick);

	while (AccumulatedSeconds >= FixedStepSeconds)
	{
		// 스텝 입력 적용과 PhysX 결과 수집
		OnPreStep.Broadcast(StepDelta);
		Scene->simulate(StepDelta);
		if (!Scene->fetchResults(true))
		{
			HTR_LOG(Error, "PxScene::fetchResults failed");
			ResetStepTime();
			return;
		}

		// 완료된 스텝의 시간 차감과 결과 전달
		AccumulatedSeconds -= FixedStepSeconds;
		OnPostStep.Broadcast(StepDelta);
	}
}

UPrimitiveComponent* FPhysicsScene::FindHitComponent(const physx::PxRigidActor* Actor) const
{
	// userData의 바디 역참조
	const auto* Body = Actor ? static_cast<const FBodyInstance*>(Actor->userData) : nullptr;
	return Body ? Body->OwnerComponent : nullptr;
}

bool FPhysicsScene::Raycast(const FRay& WorldRay, float MaxDistance, FHitResult& OutHit,
	const AActor* IgnoreActor) const
{
	return QuerySingle(WorldRay, MaxDistance, nullptr, physx::PxQuat(physx::PxIdentity),
		OutHit, IgnoreActor);
}

bool FPhysicsScene::SweepSphere(const FRay& WorldRay, float MaxDistance, float Radius,
	FHitResult& OutHit, const AActor* IgnoreActor) const
{
	// 반경 0의 광선 처리
	if (Radius == 0.0f)
	{
		return Raycast(WorldRay, MaxDistance, OutHit, IgnoreActor);
	}

	const physx::PxSphereGeometry Geometry(Radius);
	return QuerySingle(WorldRay, MaxDistance, &Geometry, physx::PxQuat(physx::PxIdentity),
		OutHit, IgnoreActor);
}

bool FPhysicsScene::SweepBox(const FRay& WorldRay, float MaxDistance, const FVector& HalfExtent,
	const FQuat& Rotation, FHitResult& OutHit, const AActor* IgnoreActor) const
{
	// 월드 크기의 반크기와 시작 회전으로 박스 구성
	const physx::PxBoxGeometry Geometry(U2PVector(HalfExtent));
	return QuerySingle(WorldRay, MaxDistance, &Geometry, U2PQuat(Rotation), OutHit, IgnoreActor);
}

bool FPhysicsScene::SweepCapsule(const FRay& WorldRay, float MaxDistance, float Radius,
	float HalfHeight, const FQuat& Rotation, FHitResult& OutHit, const AActor* IgnoreActor) const
{
	// 반구를 포함한 반높이 검증과 구체 경계 처리
	if (!std::isfinite(HalfHeight) || !std::isfinite(Radius) ||
		Radius <= 0.0f || HalfHeight < Radius)
	{
		OutHit = FHitResult();
		return false;
	}
	if (HalfHeight == Radius)
	{
		return SweepSphere(WorldRay, MaxDistance, Radius, OutHit, IgnoreActor);
	}

	// 원통 반길이 산출과 PhysX X축의 Hitori Z축 보정
	const physx::PxCapsuleGeometry Geometry(Radius, HalfHeight - Radius);
	const physx::PxQuat NativeRotation = U2PQuat(Rotation) * U2PSphylBasis;
	return QuerySingle(WorldRay, MaxDistance, &Geometry, NativeRotation, OutHit, IgnoreActor);
}


bool FPhysicsScene::QuerySingle(const FRay& WorldRay, float MaxDistance,
	const physx::PxGeometry* SweepGeometry, const physx::PxQuat& Rotation, FHitResult& OutHit,
	const AActor* IgnoreActor) const
{
	// 이전 결과 초기화와 PhysX 입력 확인
	OutHit = FHitResult();
	if (!Scene || !std::isfinite(MaxDistance) || MaxDistance <= 0.0f ||
		(SweepGeometry && (!physx::PxGeometryQuery::isValid(*SweepGeometry) || !Rotation.isUnit())))
	{
		return false;
	}

	const physx::PxVec3 Start = U2PVector(WorldRay.Origin);
	const physx::PxVec3 UnitDir = U2PVector(WorldRay.Direction.GetSafeNormal());
	const float TraceDistance = SweepGeometry ? std::min(MaxDistance, PX_MAX_SWEEP_DISTANCE) : MaxDistance;
	const physx::PxVec3 End = Start + UnitDir * TraceDistance;
	if (!Start.isFinite() || !UnitDir.isNormalized() || !End.isFinite())
	{
		return false;
	}
	OutHit.Init(P2UVector(Start), P2UVector(End));

	// 정적·동적 Shape의 사전 필터 구성
	FCollisionQueryFilterCallback Filter(*this, IgnoreActor);
	const physx::PxQueryFilterData FilterData(physx::PxQueryFlag::eSTATIC |
		physx::PxQueryFlag::eDYNAMIC | physx::PxQueryFlag::ePREFILTER);

	// 최근접 블로킹 히트 하나의 수집과 복수 Touch 결과의 확장 지점
	physx::PxLocationHit Hit;

	// 셰이프 충돌 분기
	if (SweepGeometry)
	{
		physx::PxSweepBuffer Hits;
		if (!Scene->sweep(*SweepGeometry, physx::PxTransform(Start, Rotation), UnitDir,
			TraceDistance, Hits, physx::PxHitFlag::eDEFAULT | physx::PxHitFlag::eMTD, FilterData, &Filter) ||
			!Hits.hasBlock)
		{
			return false;
		}
		Hit = Hits.block;
	}
	// 광선 충돌 체크 분기
	else
	{
		physx::PxRaycastBuffer Hits;
		if (!Scene->raycast(Start, UnitDir, TraceDistance, Hits, physx::PxHitFlag::eDEFAULT,
			FilterData, &Filter) || !Hits.hasBlock)
		{
			return false;
		}
		Hit = Hits.block;
	}

	// 이동 거리와 시작 겹침 깊이의 분리
	OutHit.bBlockingHit = true;
	OutHit.bStartPentrating = Hit.hadInitialOverlap();
	OutHit.Distance = std::max(Hit.distance, 0.0f);
	OutHit.Time = OutHit.Distance / TraceDistance;
	OutHit.PenetrationDepth = std::max(-Hit.distance, 0.0f);
	OutHit.Location = P2UVector(Start + UnitDir * OutHit.Distance);

	// 유효한 접점·법선·면 인덱스와 컴포넌트 복원
	OutHit.ImpactPoint = Hit.flags.isSet(physx::PxHitFlag::ePOSITION)
		? P2UVector(Hit.position) : OutHit.Location;
	OutHit.ImpactNormal = Hit.flags.isSet(physx::PxHitFlag::eNORMAL)
		? P2UVector(Hit.normal) : P2UVector(-UnitDir);
	OutHit.Normal = OutHit.ImpactNormal;
	if (Hit.flags.isSet(physx::PxHitFlag::eFACE_INDEX) &&
		Hit.faceIndex <= static_cast<physx::PxU32>(std::numeric_limits<int32>::max()))
	{
		OutHit.FaceIndex = static_cast<int32>(Hit.faceIndex);
	}
	OutHit.HitComponent = FindHitComponent(Hit.actor);
	return true;
}