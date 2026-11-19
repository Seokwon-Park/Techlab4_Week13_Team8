#pragma once

#include "Math/Transform.h"

class FPhysicsScene;
class UPrimitiveComponent;
struct FConstraintInstance;
class UBodySetup;

namespace physx
{
    class PxRigidActor;
    class PxRigidDynamic;
}

// 바디 초기 운동 방식
enum class EPhysicsBodyType
{
    Static,
    Dynamic,
    Kinematic
};

// 단일 리지드 액터의 수명과 강체 자세 관리
struct FBodyInstance
{
    FBodyInstance() = default;
    ~FBodyInstance();

    // 물리 콜백의 역참조를 위한 주소 고정
    FBodyInstance(const FBodyInstance&) = delete;
    FBodyInstance& operator=(const FBodyInstance&) = delete;
    FBodyInstance(FBodyInstance&&) = delete;
    FBodyInstance& operator=(FBodyInstance&&) = delete;

    // 바디 형상 생성, 초기 물성 및 씬 등록
    bool InitBody(const UBodySetup& InBodySetup, const FTransform& InWorldTransform,
        UPrimitiveComponent* InOwnerComponent, FPhysicsScene& InScene, EPhysicsBodyType InBodyType);

    // 씬 등록 해제와 액터 정리
    void TermBody();

    // 월드 위치·회전 조회
    FTransform GetWorldTransform() const;
    // 월드 자세 즉시 변경
    void SetBodyTransform(const FTransform& InWorldTransform);

    // 동적·키네마틱 모드 전환
    void SetInstanceSimulatePhysics(bool bSimulate);
    void SetKinematicTarget(const FTransform& InWorldTransform);

    // 차량 설정용 빌린 액터 포인터, 미초기화·정적 바디는 nullptr
    physx::PxRigidDynamic* GetRigidDynamic();

private:
    friend class FPhysicsScene;
    friend struct FConstraintInstance;

    // 비소유 씬·컴포넌트 참조와 소유 액터
    FPhysicsScene* Scene = nullptr;
    UPrimitiveComponent* OwnerComponent = nullptr;
    physx::PxRigidActor* Actor = nullptr;
};