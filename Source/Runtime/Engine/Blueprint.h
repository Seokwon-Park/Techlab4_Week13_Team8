#pragma once

#include "Core/EngineString.h"
#include "Containers/Array.h"

#include <filesystem>

class UClass;
class UObject;
class AActor;

// 데이터 Blueprint. UE의 Data Only Blueprint와 같은 자리다.
//
// .bp 에셋 하나가 클래스 하나다. 부모 클래스(C++ 클래스나 다른 BP)를 잇고, 기본값과 컴포넌트 구성만 바꾼다.
// 로직은 Lua로 붙인다 (기본값의 ScriptPath).
//
//   { "Version": 1, "ParentClass": "AStaticMeshActor",
//     "Defaults": { AActor::Serialize와 같은 형식 (프로퍼티 + 컴포넌트) } }
//
// 엔진이 시작할 때 Assets 아래 .bp를 모두 읽어 파일 이름("BP_Door")으로 UClass를 만들어 등록한다.
// 그래서 FindClass, SpawnActor, 클래스 목록, 씬 파일이 BP 클래스를 C++ 클래스처럼 쓴다.
// BP 클래스는 부모의 생성자와 크기를 그대로 쓰고, 만든 뒤에 기본값을 덮어쓴다.
namespace FBlueprint
{
	constexpr const char* Extension = ".bp";
	constexpr int32 FileVersion = 1;

	bool IsBlueprintPath(const FString& Path);

	// AssetRoot 아래 .bp를 모두 읽어 클래스를 등록한다. 엔진 시작 때 한 번 부른다.
	void RegisterAll(const std::filesystem::path& AssetRoot);

	// .bp 하나를 읽어 클래스를 등록한다. 이미 등록된 BP면 기본값만 새로 읽는다 (부모는 바꾸지 않는다).
	// 실패하면 nullptr이고 이유를 OutError에 쓴다.
	UClass* LoadBlueprint(const std::filesystem::path& File, FString* OutError = nullptr);

	// Template 액터의 지금 상태를 기본값으로 삼아 .bp를 쓰고 다시 읽는다. Template이 없으면 부모 기본값 그대로다.
	// ParentClass는 AActor 계열이어야 한다. 쓴 뒤 등록된 클래스를 돌려준다.
	UClass* SaveBlueprint(const std::filesystem::path& File, UClass* ParentClass, AActor* Template, FString& OutError);

	bool IsBlueprintClass(const UClass* Class);
	// BP 클래스의 에셋 경로(Assets 기준). BP가 아니면 빈 문자열.
	FString GetAssetPath(const UClass* Class);
	// BP가 아닌 가장 가까운 조상 (C++ 클래스). BP가 아니면 자기 자신.
	UClass* GetNativeClass(UClass* Class);

	// 스폰한 액터에 BP 기본값(프로퍼티와 컴포넌트)을 덮어쓴다. 부모 BP의 기본값부터 차례로 적용한다.
	// UWorld::SpawnActor가 부른다.
	void ApplyActorDefaults(AActor* Actor);
}
