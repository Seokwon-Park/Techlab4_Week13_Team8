#include "EnginePCH.h"
#include "Physics/NvClothSDK.h"

#include "Physics/PhysXSDK.h"

#include <PxFoundation.h>
#include <PxPhysics.h>
#include <NvCloth/Callbacks.h>
#include <NvCloth/Factory.h>
#include <cstdlib>

namespace NvClothSDK
{
	class FNvClothAssertHandler final : public nv::cloth::PxAssertHandler
	{
	public:
		// NvCloth assertion의 기록과 프로세스 중단
		void operator()(const char* Expression, const char* File, int Line, bool&) override
		{
			HTR_LOG(Error, "NvCloth assertion failed: {} ({}:{})", Expression, File, Line);
			std::abort();
		}
	};

	// 프로세스 공용 어서션 콜백과 CPU Factory
	static FNvClothAssertHandler GAssertHandler;
	static nv::cloth::Factory* GFactory = nullptr;

	// PhysX 콜백 등록과 CPU Factory 생성
	void Initialize()
	{
		if (GFactory)
		{
			return;
		}

		physx::PxFoundation& Foundation = PhysXSDK::GetPhysics()->getFoundation();
		nv::cloth::InitializeNvCloth(&Foundation.getAllocatorCallback(),
			&Foundation.getErrorCallback(), &GAssertHandler, nullptr);

		GFactory = NvClothCreateFactoryCPU();
		if (!GFactory)
		{
			HTR_LOG(Warning, "NvCloth CPU Factory creation failed");
			return;
		}

		HTR_LOG(Info, "NvCloth CPU Factory initialized");
	}
	// CPU Factory 해제와 포인터 초기화
	void Shutdown()
	{
		if (!GFactory)
		{
			return;
		}

		NvClothDestroyFactory(GFactory);
		GFactory = nullptr;
		HTR_LOG(Info, "NvCloth CPU Factory released");
	}

	// 월드 Solver 생성용 비소유 Factory 반환
	nv::cloth::Factory* GetFactory()
	{
		return GFactory;
	}
}