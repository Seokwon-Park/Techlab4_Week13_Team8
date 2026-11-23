#include "EnginePCH.h"
#include "Physics/CollisionQueryFilterCallback.h"

#include "Components/PrimitiveComponent.h"
#include "Physics/PhysicsScene.h"

#include <PxShape.h>

FCollisionQueryFilterCallback::FCollisionQueryFilterCallback(
	const FPhysicsScene& InScene, const AActor* InIgnoreActor)
	: PhysicsScene(InScene), IgnoreActor(InIgnoreActor)
{
}

// 트리거와 무시 대상 액터의 교차 검사 제외
physx::PxQueryHitType::Enum FCollisionQueryFilterCallback::preFilter(const physx::PxFilterData&,
	const physx::PxShape* Shape, const physx::PxRigidActor* Actor, physx::PxHitFlags&)
{
	if (Shape->getFlags().isSet(physx::PxShapeFlag::eTRIGGER_SHAPE))
	{
		return physx::PxQueryHitType::eNONE;
	}

	if (IgnoreActor)
	{
		const UPrimitiveComponent* Component = PhysicsScene.FindHitComponent(Actor);
		if (Component && Component->GetOwner() == IgnoreActor)
		{
			return physx::PxQueryHitType::eNONE;
		}
	}
	return physx::PxQueryHitType::eBLOCK;
}

// PhysX 필터 인터페이스의 후처리 반환값
physx::PxQueryHitType::Enum FCollisionQueryFilterCallback::postFilter(
	const physx::PxFilterData&, const physx::PxQueryHit&)
{
	return physx::PxQueryHitType::eBLOCK;
}