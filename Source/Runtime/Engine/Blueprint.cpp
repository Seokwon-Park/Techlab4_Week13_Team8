#include "EnginePCH.h"
#include "Engine/Blueprint.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <unordered_map>

#include "Core/EngineLog.h"
#include "Core/Misc/Paths.h"
#include "GameFramework/Actor.h"
#include "UObject/Class.h"
#include "UObject/UObjectHash.h"
#include "Json/Dom/JsonObject.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"
#include "Serialization/Formatter/JsonArchiveInputFormatter.h"

namespace
{
	struct FBlueprintData
	{
		FString AssetPath;        // Assets 기준 경로 ("Blueprints/BP_Door.bp")
		FString ParentClassName;
		TArray<uint8> FileBytes;  // 기본값을 적용할 때마다 다시 읽는다
	};

	// BP 클래스 -> 데이터. UClass는 등록된 뒤 지우지 않으므로 포인터를 키로 쓴다.
	std::unordered_map<const UClass*, FBlueprintData>& GetRegistry()
	{
		static std::unordered_map<const UClass*, FBlueprintData> Registry;
		return Registry;
	}

	const FBlueprintData* FindData(const UClass* Class)
	{
		auto It = GetRegistry().find(Class);
		return It != GetRegistry().end() ? &It->second : nullptr;
	}

	FString ToAssetPath(const std::filesystem::path& File)
	{
		std::error_code Ec;
		const std::filesystem::path Relative = std::filesystem::relative(File, "Assets", Ec);
		if (!Ec && !Relative.empty() && Relative.native().find(L"..") != 0)
			return FPaths::ToUtf8(Relative.generic_wstring());
		return FPaths::ToUtf8(File.generic_wstring());
	}

	bool ReadFileBytes(const std::filesystem::path& File, TArray<uint8>& OutBytes)
	{
		std::ifstream Stream(File, std::ios::binary | std::ios::ate);
		if (!Stream)
			return false;
		const std::streamoff Size = Stream.tellg();
		if (Size <= 0 || Size > 64ll * 1024 * 1024)
			return false;
		OutBytes.SetNum(static_cast<int32>(Size));
		Stream.seekg(0);
		Stream.read(reinterpret_cast<char*>(OutBytes.GetData()), Size);
		return static_cast<bool>(Stream);
	}

	// .bp의 머리(버전, 부모 클래스 이름)만 읽는다.
	bool ReadHeader(const TArray<uint8>& Bytes, FString& OutParentClass, FString& OutError)
	{
		FMemoryReader Reader(Bytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError())
		{
			OutError = "not valid JSON";
			return false;
		}
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		int32 Version = 0;
		Root << SA_VALUE("Version", Version) << SA_VALUE("ParentClass", OutParentClass);
		Ar.Close();
		if (Version != FBlueprint::FileVersion)
		{
			OutError = "unsupported version " + std::to_string(Version);
			return false;
		}
		if (OutParentClass.empty())
		{
			OutError = "ParentClass is empty";
			return false;
		}
		return true;
	}

	// Data의 "Defaults"를 Object에 읽어 들인다. bActor면 컴포넌트까지, 아니면 프로퍼티만 읽는다.
	void ApplyDefaultsRecord(const FBlueprintData& Data, UObject* Object, bool bActor)
	{
		FMemoryReader Reader(Data.FileBytes, true);
		FJsonArchiveInputFormatter Formatter(Reader);
		if (Reader.IsError())
			return;
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		FStructuredArchive::FRecord Defaults = Root.EnterRecord("Defaults");
		if (bActor)
			static_cast<AActor*>(Object)->Serialize(Defaults);
		else
			Object->UObject::Serialize(Defaults);   // 프로퍼티만. CDO에는 컴포넌트를 만들지 않는다
		Ar.Close();
		if (Reader.IsError())
			HTR_LOG(Warning, "Blueprint: defaults of {} could not be read", Data.AssetPath);
	}

	// Class부터 위로 올라가며 BP 클래스를 모아, 가장 위(부모 쪽) BP부터 적용한다.
	void ApplyChain(UClass* Class, UObject* Object, bool bActor)
	{
		TArray<const FBlueprintData*> Chain;
		for (UClass* It = Class; It; It = It->Super)
			if (const FBlueprintData* Data = FindData(It))
				Chain.Add(Data);
		for (int32 Index = Chain.Num() - 1; Index >= 0; --Index)
			ApplyDefaultsRecord(*Chain[Index], Object, bActor);
	}

	// CDO를 만들 때 UClass::CreateDefaultObject가 부른다. 프로퍼티 기본값만 덮어쓴다.
	void ApplyClassDefaultObjectDefaults(UObject* DefaultObject)
	{
		if (DefaultObject)
			ApplyChain(DefaultObject->GetClass(), DefaultObject, false);
	}

	// Name이라는 BP 클래스를 만들거나 찾아 데이터를 채운다. 부모가 아직 없으면 nullptr.
	UClass* RegisterClassFromData(const FString& ClassName, FBlueprintData&& Data, FString& OutError)
	{
		UClass* Parent = FindClass(Data.ParentClassName);
		if (!Parent)
		{
			OutError = "parent class '" + Data.ParentClassName + "' not found";
			return nullptr;
		}
		if (!Parent->IsChildOf(AActor::StaticClass()))
		{
			OutError = "parent class '" + Data.ParentClassName + "' is not an Actor";
			return nullptr;
		}
		if (!Parent->Constructor)
		{
			OutError = "parent class '" + Data.ParentClassName + "' is abstract";
			return nullptr;
		}

		UClass* Class = FindClass(ClassName);
		if (Class && !FBlueprint::IsBlueprintClass(Class))
		{
			OutError = "a C++ class named '" + ClassName + "' already exists";
			return nullptr;
		}
		if (Class && Class->Super != Parent)
		{
			// 부모를 바꾸면 이미 만든 객체와 자식 목록이 어긋나므로 다시 시작할 때까지 미룬다.
			HTR_LOG(Warning, "Blueprint: parent of {} changed to {}; restart the editor to apply it", ClassName, Data.ParentClassName);
			Data.ParentClassName = Class->Super->Name;
		}
		if (!Class)
		{
			// 클래스는 프로그램이 끝날 때까지 산다 (C++ 클래스의 static UClass와 같다).
			Class = new UClass();
			Class->Name = ClassName;
			Class->Super = Parent;
			Class->Size = Parent->Size;
			Class->Constructor = Parent->Constructor;
			Class->PostConstructDefaults = &ApplyClassDefaultObjectDefaults;
			RegisterClass(Class);
		}

		GetRegistry()[Class] = std::move(Data);

		// 이미 CDO가 있으면 새 기본값을 다시 적용한다 (편집한 BP를 다시 읽은 경우).
		if (Class->ClassDefaultObject)
			ApplyClassDefaultObjectDefaults(Class->ClassDefaultObject);
		return Class;
	}
}

bool FBlueprint::IsBlueprintPath(const FString& Path)
{
	FString Ext = FPaths::FromUtf8(Path).extension().string();
	std::transform(Ext.begin(), Ext.end(), Ext.begin(), [](unsigned char C) { return static_cast<char>(std::tolower(C)); });
	return Ext == Extension;
}

UClass* FBlueprint::LoadBlueprint(const std::filesystem::path& File, FString* OutError)
{
	FString Error;
	FBlueprintData Data;
	Data.AssetPath = ToAssetPath(File);
	UClass* Result = nullptr;
	if (!ReadFileBytes(File, Data.FileBytes))
		Error = "cannot read file";
	else if (ReadHeader(Data.FileBytes, Data.ParentClassName, Error))
		Result = RegisterClassFromData(FPaths::ToUtf8(File.stem()), std::move(Data), Error);

	if (!Result)
	{
		HTR_LOG(Warning, "Blueprint: {} skipped ({})", FPaths::ToUtf8(File.generic_wstring()), Error);
		if (OutError) *OutError = Error;
	}
	return Result;
}

void FBlueprint::RegisterAll(const std::filesystem::path& AssetRoot)
{
	std::error_code Ec;
	if (!std::filesystem::exists(AssetRoot, Ec))
		return;

	// BP의 부모가 다른 BP일 수 있다. 부모가 아직 등록되지 않아 실패한 것은 다음 바퀴에 다시 시도한다.
	TArray<std::filesystem::path> Pending;
	for (const auto& Entry : std::filesystem::recursive_directory_iterator(AssetRoot, Ec))
		if (Entry.is_regular_file() && IsBlueprintPath(FPaths::ToUtf8(Entry.path())))
			Pending.Add(Entry.path());

	int32 Registered = 0;
	while (!Pending.IsEmpty())
	{
		TArray<std::filesystem::path> Retry;
		for (const std::filesystem::path& File : Pending)
		{
			FBlueprintData Data;
			Data.AssetPath = ToAssetPath(File);
			FString Error;
			if (!ReadFileBytes(File, Data.FileBytes) || !ReadHeader(Data.FileBytes, Data.ParentClassName, Error))
			{
				HTR_LOG(Warning, "Blueprint: {} skipped ({})", Data.AssetPath, Error.empty() ? FString("cannot read file") : Error);
				continue;
			}
			if (!FindClass(Data.ParentClassName))
			{
				Retry.Add(File);
				continue;
			}
			if (RegisterClassFromData(FPaths::ToUtf8(File.stem()), std::move(Data), Error))
				++Registered;
			else
				HTR_LOG(Warning, "Blueprint: {} skipped ({})", FPaths::ToUtf8(File.generic_wstring()), Error);
		}
		// 한 바퀴 동안 하나도 못 풀었으면 부모를 끝내 찾지 못한 것이다.
		if (Retry.Num() == Pending.Num())
		{
			for (const std::filesystem::path& File : Retry)
				LoadBlueprint(File);   // 실패 이유를 로그에 남긴다
			break;
		}
		Pending = std::move(Retry);
	}
	if (Registered > 0)
		HTR_LOG(Info, "Blueprint: {} classes registered", Registered);
}

UClass* FBlueprint::SaveBlueprint(const std::filesystem::path& File, UClass* ParentClass, AActor* Template, FString& OutError)
{
	if (!ParentClass || !ParentClass->IsChildOf(AActor::StaticClass()))
	{
		OutError = "parent class must be an Actor";
		return nullptr;
	}

	// 템플릿이 없으면(새로 만드는 BP) 부모 클래스의 기본 객체를 그대로 기본값으로 쓴다.
	// 비워 두면 읽을 때마다 "필드 없음" 경고가 나오므로 부모 값을 채워 둔다.
	if (!Template)
		Template = Cast<AActor>(ParentClass->GetDefaultObject());

	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	Writer.SetIsPersistent(true);   // 씬 파일처럼 Transient는 쓰지 않는다
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		int32 Version = FileVersion;
		FString ParentName = ParentClass->Name;
		Root << SA_VALUE("Version", Version) << SA_VALUE("ParentClass", ParentName);
		FStructuredArchive::FRecord Defaults = Root.EnterRecord("Defaults");
		if (Template)
			Template->Serialize(Defaults);
		Ar.Close();
	}
	if (Writer.IsError())
	{
		OutError = "failed to serialize defaults";
		return nullptr;
	}

	std::error_code Ec;
	std::filesystem::create_directories(File.parent_path(), Ec);
	{
		std::ofstream Stream(File, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			OutError = "cannot write " + FPaths::ToUtf8(File.generic_wstring());
			return nullptr;
		}
		Stream.write(reinterpret_cast<const char*>(Bytes.GetData()), Bytes.Num());
	}
	return LoadBlueprint(File, &OutError);
}

bool FBlueprint::IsBlueprintClass(const UClass* Class)
{
	return FindData(Class) != nullptr;
}

FString FBlueprint::GetAssetPath(const UClass* Class)
{
	const FBlueprintData* Data = FindData(Class);
	return Data ? Data->AssetPath : FString();
}

UClass* FBlueprint::GetNativeClass(UClass* Class)
{
	while (Class && IsBlueprintClass(Class))
		Class = Class->Super;
	return Class;
}

void FBlueprint::ApplyActorDefaults(AActor* Actor)
{
	if (Actor)
		ApplyChain(Actor->GetClass(), Actor, true);
}

bool FBlueprint::SetDefaultStringProperty(UClass* BlueprintClass, const FString& PropertyName, const FString& Value, FString& OutError)
{
	const FBlueprintData* Data = FindData(BlueprintClass);
	if (!Data)
	{
		OutError = "not a blueprint class";
		return false;
	}

	// 기본값 전체를 다시 쓰지 않고 그 프로퍼티만 고친다. 컴포넌트 구성 등 나머지는 그대로 남는다.
	FJsonObject Root = FJsonObject::parse(Data->FileBytes.begin(), Data->FileBytes.end(), nullptr, false);
	if (Root.is_discarded() || !Root.is_object())
	{
		OutError = "file is not valid JSON";
		return false;
	}
	Root["Defaults"]["Properties"][PropertyName] = Value;
	const std::string Text = Root.dump(1, '\t');

	const std::filesystem::path File = FPaths::FromUtf8(Data->AssetPath.find(':') == FString::npos
		? "Assets/" + Data->AssetPath : Data->AssetPath);
	{
		std::ofstream Stream(File, std::ios::binary | std::ios::trunc);
		if (!Stream)
		{
			OutError = "cannot write " + Data->AssetPath;
			return false;
		}
		Stream.write(Text.data(), static_cast<std::streamsize>(Text.size()));
	}
	return LoadBlueprint(File, &OutError) != nullptr;
}
