#include "EnginePCH.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysicsSettings.h"
#include "Core/EngineLog.h"
#include "Physics/PhysXSDK.h"

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