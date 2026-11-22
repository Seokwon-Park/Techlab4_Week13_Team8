#include "EnginePCH.h"
#include "Physics/BodyInstance.h"

#include "Physics/BodySetup.h"
#include "Physics/PhysicalMaterial.h"
#include "Physics/PhysicsSettings.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSDK.h"

#include <cmath>
#include <memory>
#include <extensions/PxRigidActorExt.h>
#include <extensions/PxRigidBodyExt.h>

FBodyInstance::~FBodyInstance()
{
    // 액터 수명의 최종 정리
    TermBody();
}

bool FBodyInstance::InitBody(const UBodySetup& InBodySetup, const FTransform& InWorldTransform,
    UPrimitiveComponent* InOwnerComponent, FPhysicsScene& InScene, EPhysicsBodyType InBodyType)
{
    // 중복 초기화와 필수 PhysX 객체 확인
    physx::PxPhysics* Physics = PhysXSDK::GetPhysics();
    physx::PxScene* NativeScene = InScene.GetPxScene();
    if (Actor || !Physics || !NativeScene)
    {
        return false;
    }

    // 강체 자세와 양의 균일 스케일 확인
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    const physx::PxVec3 Scale = U2PVector(InWorldTransform.Scale);
    const float ShapeScale = Scale.x;
    const float ScaleTolerance = ShapeScale * 1e-4f;
    if (!WorldPose.isValid() || !Scale.isFinite() || Scale.minElement() <= 0.0f ||
        !FMath::IsNearlyEqual(ShapeScale, Scale.y, ScaleTolerance) ||
        !FMath::IsNearlyEqual(ShapeScale, Scale.z, ScaleTolerance))
    {
        return false;
    }

    // 지원 형상과 재질 계수 확인
    const FKAggregateGeom& Geom = InBodySetup.AggGeom;
    const float Friction = InBodySetup.Friction;
    const float Restitution = InBodySetup.Restitution;
    if (Geom.GetelementCount() == 0 || Geom.ConvexElems.Num() > 0 ||
        !std::isfinite(Friction) || Friction < 0.0f ||
        !(Restitution >= 0.0f && Restitution <= 1.0f))
    {
        return false;
    }

    // 실패 경로에서 PhysX 생성 참조의 자동 해제
    const auto Release = [](auto* Object) { Object->release(); };

    std::unique_ptr<physx::PxMaterial, decltype(Release)> Material(
        Physics->createMaterial(Friction, Friction, Restitution), Release);
    if (!Material)
    {
        return false;
    }

    const bool bStatic = InBodyType == EPhysicsBodyType::Static;
    std::unique_ptr<physx::PxRigidActor, decltype(Release)> NewActor(
        bStatic ? static_cast<physx::PxRigidActor*>(Physics->createRigidStatic(WorldPose))
        : Physics->createRigidDynamic(WorldPose),
        Release);
    if (!NewActor)
    {
        return false;
    }

    // 미등록 액터에 형상과 로컬 자세 설정
    TArray<physx::PxShape*> ShapesExcludedFromMass;
    const auto AttachShape = [&](const auto& Geometry, const physx::PxTransform& LocalPose,
        const FKShapeElem& Elem)
        {
            if (!Geometry.isValid() || !LocalPose.isValid())
            {
                return false;
            }

            physx::PxShape* Shape = physx::PxRigidActorExt::createExclusiveShape(
                *NewActor, Geometry, *Material);
            if (!Shape)
            {
                return false;
            }

            Shape->setLocalPose(LocalPose);
            if (!bStatic && !Elem.bContributeToMass)
            {
                ShapesExcludedFromMass.Add(Shape);
            }
            return true;
        };

    for (const FKSphereElem& Elem : Geom.SphereElems)
    {
        const physx::PxSphereGeometry Geometry(Elem.Radius * ShapeScale);
        const physx::PxTransform LocalPose(U2PVector(Elem.Center * ShapeScale));
        if (!AttachShape(Geometry, LocalPose, Elem))
        {
            return false;
        }
    }

    const float HalfScale = 0.5f * ShapeScale;
    for (const FKBoxElem& Elem : Geom.BoxElems)
    {
        const physx::PxBoxGeometry Geometry(Elem.X * HalfScale, Elem.Y * HalfScale, Elem.Z * HalfScale);
        const physx::PxTransform LocalPose(U2PVector(Elem.Center * ShapeScale), U2PQuat(Elem.Rotation.Quaternion()));
        if (!AttachShape(Geometry, LocalPose, Elem))
        {
            return false;
        }
    }

    for (const FKSphylElem& Elem : Geom.SphylElems)
    {
        const physx::PxCapsuleGeometry Geometry(Elem.Radius * ShapeScale, Elem.Length * HalfScale);
        const physx::PxTransform LocalPose = U2PSphylLocalPose(Elem.Center * ShapeScale, Elem.Rotation);
        if (!AttachShape(Geometry, LocalPose, Elem))
        {
            return false;
        }
    }

    if (physx::PxRigidDynamic* DynamicActor = NewActor->is<physx::PxRigidDynamic>())
    {
        const float OverrideMass = InBodySetup.MassInKgOverride;
        if (NewActor->getNbShapes() == static_cast<physx::PxU32>(ShapesExcludedFromMass.Num()) ||
            (InBodySetup.bOverrideMass && (!std::isfinite(OverrideMass) || OverrideMass <= 0.0f)))
        {
            return false;
        }

        // 비질량 형상을 관성 계산 동안만 제외
        for (physx::PxShape* Shape : ShapesExcludedFromMass)
        {
            Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
        }

        constexpr float DefaultDensityKgPerM3 = 1000.0f;
        const bool bMassReady = InBodySetup.bOverrideMass
            ? physx::PxRigidBodyExt::setMassAndUpdateInertia(*DynamicActor, OverrideMass)
            : physx::PxRigidBodyExt::updateMassAndInertia(*DynamicActor, DefaultDensityKgPerM3);
        if (!bMassReady)
        {
            return false;
        }

        for (physx::PxShape* Shape : ShapesExcludedFromMass)
        {
            Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, true);
        }
        DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC,
            InBodyType == EPhysicsBodyType::Kinematic);
    }

    // 씬 등록 성공 후 바디 소유권과 역참조 확정
    NativeScene->addActor(*NewActor);
    if (NewActor->getScene() != NativeScene)
    {
        return false;
    }

    Actor = NewActor.release();
    Scene = &InScene;
    OwnerComponent = InOwnerComponent;
    Actor->userData = this;
    return true;
}

void FBodyInstance::TermBody()
{
    // 콜백 역참조 해제와 네이티브 액터 반환
    if (Actor)
    {
        Actor->userData = nullptr;
        Actor->release();
        Actor = nullptr;
    }

    Scene = nullptr;
    OwnerComponent = nullptr;
}

FTransform FBodyInstance::GetWorldTransform() const
{
    // 월드 Transfrom 반환
    return Actor ? P2UTransform(Actor->getGlobalPose()) : FTransform::Identity;
}

// 호출 즉시 이동
void FBodyInstance::SetBodyTransform(const FTransform& InWorldTransform)
{
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    if (!Actor || !WorldPose.isValid())
    {
        return;
    }

    // 키네마틱 순간 이동 시 이전 목표 덮어쓰기
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (DynamicActor &&
        DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) &&
        !DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        DynamicActor->setKinematicTarget(WorldPose);
    }

    // 현재 강체 자세의 즉시 변경
    Actor->setGlobalPose(WorldPose);
}

// 동적 시뮬레이션 API
void FBodyInstance::SetInstanceSimulatePhysics(bool bSimulate)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor || DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        return;
    }

    // 요청 모드가 현재 모드와 같은 경우의 생략
    const bool bKinematic = !bSimulate;
    if (DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) == bKinematic)
    {
        return;
    }

    DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bKinematic);
    if (bSimulate)
    {
        // 동적 전환 직후의 시뮬레이션 재개
        DynamicActor->wakeUp();
    }
}

void FBodyInstance::SetKinematicTarget(const FTransform& InWorldTransform)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor ||
        !DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
        DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        return;
    }

    // 다음 물리 스텝에서 도달할 월드 자세 등록
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    if (WorldPose.isValid())
    {
        DynamicActor->setKinematicTarget(WorldPose);
    }
}

physx::PxRigidDynamic* FBodyInstance::GetRigidDynamic()
{
    // 액터가 Dynamic일시 반환
    return Actor ? Actor->is<physx::PxRigidDynamic>() : nullptr;
}

// false는 대입, true는 가산
void FBodyInstance::SetLinearVelocity(const FVector& NewVel, bool bAddToCurrent)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor ||
        DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
        DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        return;
    }

    // 기존 속도 가산 또는 새 속도로 교체
    const physx::PxVec3 Velocity = U2PVector(NewVel);
    const physx::PxVec3 FinalVelocity = bAddToCurrent ? DynamicActor->getLinearVelocity() + Velocity : Velocity;
    if (FinalVelocity.isFinite())
    {
        DynamicActor->setLinearVelocity(FinalVelocity);
    }
}