#include "EnginePCH.h"
#include "Physics/PhysXSimEventCallback.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Physics/PhysicsScene.h"
#include "UObject/UObjectHash.h"
#include <extensions/PxDefaultSimulationFilterShader.h>

FPhysXSimEventCallback::FPhysXSimEventCallback(const FPhysicsScene& InScene)
    : Scene(InScene)
{
}

physx::PxFilterFlags FPhysXSimEventCallback::FilterShader(
    physx::PxFilterObjectAttributes AttributesA, physx::PxFilterData FilterDataA,
    physx::PxFilterObjectAttributes AttributesB, physx::PxFilterData FilterDataB,
    physx::PxPairFlags& OutPairFlags, const void* ConstantBlock, physx::PxU32 ConstantBlockSize)
{
    using namespace physx;

    // SDK 기본 필터 정책 유지
    const PxFilterFlags Result = PxDefaultSimulationFilterShader(AttributesA, FilterDataA,
        AttributesB, FilterDataB, OutPairFlags, ConstantBlock, ConstantBlockSize);

    // 충돌 노티파이 추가
    if (Result == PxFilterFlag::eDEFAULT && 
        !PxFilterObjectIsTrigger(AttributesA) && !PxFilterObjectIsTrigger(AttributesB))
    {
        OutPairFlags |= PxPairFlag::eNOTIFY_TOUCH_FOUND;
    }
    return Result;
}

bool FPhysXSimEventCallback::FComponentPair::Matches(const FComponentPair& Other) const
{
    // 수신 순서와 무관한 컴포넌트 쌍 비교
    return (A == Other.A && B == Other.B && UuidA == Other.UuidA && UuidB == Other.UuidB)
        || (A == Other.B && B == Other.A && UuidA == Other.UuidB && UuidB == Other.UuidA);
}

bool FPhysXSimEventCallback::FComponentPair::IsLive() const
{
    // 삭제와 주소 재사용 확인 후 등록 상태 검사
    uint32 CurrentA = 0;
    uint32 CurrentB = 0;
    return TryGetLiveObjectUUID(A, nullptr, CurrentA) && CurrentA == UuidA
        && TryGetLiveObjectUUID(B, nullptr, CurrentB) && CurrentB == UuidB
        && A->IsRegistered() && B->IsRegistered();
}