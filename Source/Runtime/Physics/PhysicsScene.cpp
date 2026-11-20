#include "EnginePCH.h"
#include "Physics/PhysicsScene.h"

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

	// SDK의 허용 오차와 Hitori의 Z축 중력을 사용한 월드 씬 생성
	physx::PxSceneDesc Desc(Physics->getTolerancesScale());
	Desc.gravity = physx::PxVec3(0.0f, 0.0f, -9.81f);
	Desc.cpuDispatcher = Dispatcher;
	Desc.filterShader = physx::PxDefaultSimulationFilterShader;
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

void FPhysicsScene::Tick(float DeltaSeconds)
{
	if (!Scene || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// 60Hz 고정 스텝과 월드 틱당 최대 여섯 번의 계산
	constexpr double FixedStepSeconds = 1.0 / 60.0;
	constexpr int MaxStepsPerTick = 6;
	constexpr float StepDelta = static_cast<float>(FixedStepSeconds);
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