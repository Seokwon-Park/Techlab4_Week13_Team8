#include "EnginePCH.h"
#include "Physics/PhysXSDK.h"
#include "Physics/PhysicsSettings.h"
#include "Core/EngineLog.h"

#include <PxFoundation.h>
#include <PxPhysics.h>
#include <PxPhysicsVersion.h>
#include <common/PxTolerancesScale.h>
#include <extensions/PxDefaultAllocator.h>
#include <extensions/PxDefaultErrorCallback.h>
#include <extensions/PxDefaultCpuDispatcher.h>
#include <extensions/PxExtensionsAPI.h>

namespace PhysXSDK
{
	// Foundation에 전달하는 콜백의 프로세스 수명 보장
	static physx::PxDefaultAllocator GAllocator;
	static physx::PxDefaultErrorCallback GErrorCallback;

	// 프로세스 공용 SDK 객체
	static physx::PxFoundation* GFoundation = nullptr;
	static physx::PxPhysics* GPhysics = nullptr;
	static physx::PxDefaultCpuDispatcher* GCpuDispatcher = nullptr;
	static bool bExtensionsInitialized = false;

	bool Initialize()
	{
		if (GPhysics)
		{
			return true;
		}

		// 메모리 할당과 오류 보고를 담당하는 기반 객체 생성
		GFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, GAllocator, GErrorCallback);
		if (!GFoundation)
		{
			HTR_LOG(Error, "PxCreateFoundation failed");
			return false;
		}

		// 기반 객체에 의존하는 PhysX 인스턴스 생성
		GPhysics = PxCreatePhysics(PX_PHYSICS_VERSION, *GFoundation, physx::PxTolerancesScale());
		if (!GPhysics)
		{
			HTR_LOG(Error, "PxCreatePhysics failed");
			Shutdown();
			return false;
		}

		// 기본 Dispatcher 사용을 위한 Extensions 초기화
		bExtensionsInitialized = PxInitExtensions(*GPhysics, nullptr);
		if (!bExtensionsInitialized)
		{
			HTR_LOG(Error, "PxInitExtensions failed");
			Shutdown();
			return false;
		}

		// 프로젝트 워커 수에 따른 CPU 디스패처 생성. 기본 0개
		GCpuDispatcher = physx::PxDefaultCpuDispatcherCreate(FPhysicsSettings::Get().CpuDispatcherWorkerCount);
		if (!GCpuDispatcher)
		{
			HTR_LOG(Error, "PxDefaultCpuDispatcherCreate failed");
			Shutdown();
			return false;
		}

		HTR_LOG(Info, "PhysX SDK initialized");
		return true;
	}

	void Shutdown()
	{
		// 의존 순서의 역순으로 SDK 객체 해제
		if (GCpuDispatcher)
		{
			GCpuDispatcher->release();
			GCpuDispatcher = nullptr;
		}

		if (bExtensionsInitialized)
		{
			PxCloseExtensions();
			bExtensionsInitialized = false;
		}

		if (GPhysics)
		{
			GPhysics->release();
			GPhysics = nullptr;
		}

		if (GFoundation)
		{
			GFoundation->release();
			GFoundation = nullptr;
			HTR_LOG(Info, "PhysX SDK released");
		}
	}

	// 씬 생성에 필요한 공용 객체 반환
	physx::PxPhysics* GetPhysics()
	{
		return GPhysics;
	}

	physx::PxCpuDispatcher* GetCpuDispatcher()
	{
		return GCpuDispatcher;
	}
}