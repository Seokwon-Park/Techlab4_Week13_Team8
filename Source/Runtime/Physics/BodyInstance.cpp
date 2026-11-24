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
#include <PxSceneLock.h>

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

    // 강체 자세와 양의 스케일 확인. PhysX에는 미러가 없어 음수 스케일은 받지 않는다
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    const FVector& Scale = InWorldTransform.Scale;
    if (!WorldPose.isValid() || !U2PVector(Scale).isFinite() || U2PVector(Scale).minElement() <= 0.0f)
    {
        return false;
    }

    // 재질 계수 선택과 지원 형상 확인
    const FKAggregateGeom& Geom = InBodySetup.AggGeom;
    const UPhysicalMaterial* PhysMaterial = InBodySetup.PhysMaterial;
    const float StaticFriction = PhysMaterial ? PhysMaterial->StaticFriction : InBodySetup.Friction;
    const float DynamicFriction = PhysMaterial ? PhysMaterial->Friction : InBodySetup.Friction;
    const float Restitution = PhysMaterial ? PhysMaterial->Restitution : InBodySetup.Restitution;
    if (Geom.GetElementCount() == 0 || Geom.ConvexElems.Num() > 0 ||
        !std::isfinite(StaticFriction) || StaticFriction < 0.0f ||
        !std::isfinite(DynamicFriction) || DynamicFriction < 0.0f ||
        !(Restitution >= 0.0f && Restitution <= 1.0f))
    {
        return false;
    }

    // 실패 경로에서 PhysX 생성 참조의 자동 해제
    const auto Release = [](auto* Object) { Object->release(); };

    std::unique_ptr<physx::PxMaterial, decltype(Release)> Material(
        Physics->createMaterial(StaticFriction, DynamicFriction, Restitution), Release);
    if (!Material)
    {
        return false;
    }

    // 항목별 프로젝트 기본값과 재질 오버라이드 적용
    const FPhysicsSettings& Settings = FPhysicsSettings::Get();
    const EPhysicalMaterialCombineMode FrictionMode =
        PhysMaterial && PhysMaterial->bOverrideFrictionCombineMode
        ? PhysMaterial->FrictionCombineMode : Settings.DefaultFrictionCombineMode;
    const EPhysicalMaterialCombineMode RestitutionMode =
        PhysMaterial && PhysMaterial->bOverrideRestitutionCombineMode
        ? PhysMaterial->RestitutionCombineMode : Settings.DefaultRestitutionCombineMode;

    Material->setFrictionCombineMode(U2PCombineMode(FrictionMode));
    Material->setRestitutionCombineMode(U2PCombineMode(RestitutionMode));

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

    // 비균일 스케일은 회전 없는 Box만 정확하다. 나머지는 PhysicsDebugDraw와 같은 규칙으로 근사해
    // 그려지는 와이어프레임과 충돌 크기를 맞춘다
    const float SphereScale = std::max({ Scale.X, Scale.Y, Scale.Z });
    for (const FKSphereElem& Elem : Geom.SphereElems)
    {
        const physx::PxSphereGeometry Geometry(Elem.Radius * SphereScale);
        const physx::PxTransform LocalPose(U2PVector(Elem.Center * Scale));
        if (!AttachShape(Geometry, LocalPose, Elem))
        {
            return false;
        }
    }

    // 요소가 회전해 있으면 shear를 버리고 박스 로컬 축에 그대로 곱한다
    for (const FKBoxElem& Elem : Geom.BoxElems)
    {
        const physx::PxBoxGeometry Geometry(0.5f * Elem.X * Scale.X, 0.5f * Elem.Y * Scale.Y, 0.5f * Elem.Z * Scale.Z);
        const physx::PxTransform LocalPose(U2PVector(Elem.Center * Scale), U2PQuat(Elem.Rotation.Quaternion()));
        if (!AttachShape(Geometry, LocalPose, Elem))
        {
            return false;
        }
    }

    const float SphylRadiusScale = std::max(Scale.X, Scale.Y);
    for (const FKSphylElem& Elem : Geom.SphylElems)
    {
        const physx::PxCapsuleGeometry Geometry(Elem.Radius * SphylRadiusScale, 0.5f * Elem.Length * Scale.Z);
        const physx::PxTransform LocalPose = U2PSphylLocalPose(Elem.Center * Scale, Elem.Rotation);
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
        const bool bKinematic = InBodyType == EPhysicsBodyType::Kinematic;
        DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bKinematic);
        DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, !bKinematic && bUseCCD);
    }

    // 질량 계산은 시뮬레이션 Shape만 보므로 질량을 정한 뒤에 끈다
    if (CollisionEnabled == ECollisionEnabled::QueryOnly)
    {
        TArray<physx::PxShape*> Shapes;
        Shapes.SetNum(static_cast<int32>(NewActor->getNbShapes()));
        NewActor->getShapes(Shapes.GetData(), Shapes.Num());
        for (physx::PxShape* Shape : Shapes)
        {
            Shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
            physx::PxFilterData QueryFilter = Shape->getQueryFilterData();
            QueryFilter.word3 |= QueryOnlyShapeQueryBit;
            Shape->setQueryFilterData(QueryFilter);
        }
    }

    // 씬 등록과 바디 역참조 설정의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*NativeScene);
    NativeScene->addActor(*NewActor);
    if (NewActor->getScene() != NativeScene)
    {
        return false;
    }

    Actor = NewActor.release();
    Scene = &InScene;
    OwnerComponent = InOwnerComponent;
    Scale3D = InWorldTransform.Scale;
    Actor->userData = this;
    return true;
}

void FBodyInstance::TermBody()
{
    // 콜백 역참조 해제와 네이티브 액터 반환
    if (Actor)
    {
        const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
        Actor->userData = nullptr;
        Actor->release();
        Actor = nullptr;
    }

    Scene = nullptr;
    OwnerComponent = nullptr;
}

FTransform FBodyInstance::GetWorldTransform() const
{
    if (!Actor)
    {
        return FTransform::Identity;
    }

    // 네이티브 바디 자세 조회의 읽기 잠금
    const physx::PxSceneReadLock ReadLock(*Scene->GetPxScene());
    return P2UTransform(Actor->getGlobalPose());
}

// 호출 즉시 이동
void FBodyInstance::SetBodyTransform(const FTransform& InWorldTransform)
{
    const physx::PxTransform WorldPose = U2PTransform(InWorldTransform);
    if (!Actor || !WorldPose.isValid())
    {
        return;
    }

    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());

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
    if (!DynamicActor)
    {
        return;
    }

    // 바디 플래그 조회와 모드 전환의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    if (DynamicActor->getActorFlags().isSet(physx::PxActorFlag::eDISABLE_SIMULATION))
    {
        return;
    }

    // 요청 모드가 현재 모드와 같은 경우의 생략
    const bool bKinematic = !bSimulate;
    if (DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) == bKinematic)
    {
        return;
    }

    if (bKinematic)
    {
        // 키네마틱 전환 전 CCD 해제
        DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, false);
    }

    DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, bKinematic);

    if (bSimulate)
    {
        // 동적 복귀 시 CCD 복원과 시뮬레이션 재개
        DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, bUseCCD);
        DynamicActor->wakeUp();
    }
}

void FBodyInstance::SetKinematicTarget(const FTransform& InWorldTransform)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor)
    {
        return;
    }

    // 바디 플래그 조회와 키네마틱 목표 설정의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    if (!DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
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

void FBodyInstance::SetUseCCD(bool bInUseCCD)
{
    physx::PxRigidDynamic* DynamicActor = GetRigidDynamic();
    if (!DynamicActor)
    {
        // 초기화 전 또는 정적 바디의 CCD 설정 저장
        bUseCCD = bInUseCCD;
        return;
    }

    // 등록된 바디의 CCD 설정과 플래그 변경 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    bUseCCD = bInUseCCD;

    if (!DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC))
    {
        // 동적 모드의 CCD 설정 즉시 반영
        DynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, bUseCCD);
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
    if (!DynamicActor)
    {
        return;
    }

    // 바디 플래그 조회와 선속도 갱신의 쓰기 잠금
    const physx::PxSceneWriteLock WriteLock(*Scene->GetPxScene());
    if (DynamicActor->getRigidBodyFlags().isSet(physx::PxRigidBodyFlag::eKINEMATIC) ||
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