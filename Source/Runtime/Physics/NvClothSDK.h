#pragma once

namespace nv::cloth
{
	class Factory;
}

// 프로세스 공용 NvCloth 초기화와 종료
namespace NvClothSDK
{
	void Initialize();
	void Shutdown();

	// 월드 Solver 생성용 비소유 Factory 조회
	nv::cloth::Factory* GetFactory();
}