#pragma once

#include "Core/Delegate/Delegate.h"

namespace physx
{
	class PxScene;
}

DECLARE_DELEGATE(FPhysicsStepDelegate, float);

class FPhysicsScene
{
public:
	FPhysicsScene();
	~FPhysicsScene();

	// 네이티브 씬 소유권 중복 방지
	FPhysicsScene(const FPhysicsScene&) = delete;
	FPhysicsScene& operator=(const FPhysicsScene&) = delete;

	// 월드 시간 누적과 고정 스텝 실행
	void Tick(float DeltaSeconds);
	void ResetStepTime() { AccumulatedSeconds = 0.0; }

	// 바디 생성과 차량 쿼리용 비소유 씬 접근
	physx::PxScene* GetPxScene() const { return Scene; }

	// 물리 계산 전후 Delegate
	FPhysicsStepDelegate OnPreStep;
	FPhysicsStepDelegate OnPostStep;

private:
	physx::PxScene* Scene = nullptr;
	double AccumulatedSeconds = 0.0;
};