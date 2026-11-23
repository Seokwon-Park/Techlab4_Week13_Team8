#pragma once

#include <PxQueryFiltering.h>

class AActor;
class FPhysicsScene;

// UE 4.27 CollisionQueryFilterCallback.h의 경량화
// 무시 목록과 채널별 응답 판정의 확장 지점
class FCollisionQueryFilterCallback final : public physx::PxQueryFilterCallback
{
public:
	FCollisionQueryFilterCallback(const FPhysicsScene& InScene, const AActor* InIgnoreActor);

	physx::PxQueryHitType::Enum preFilter(const physx::PxFilterData&, const physx::PxShape* Shape,
		const physx::PxRigidActor* Actor, physx::PxHitFlags&) override;
	physx::PxQueryHitType::Enum postFilter(const physx::PxFilterData&, const physx::PxQueryHit&) override;

private:
	const FPhysicsScene& PhysicsScene;
	const AActor* IgnoreActor;
};