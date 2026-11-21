#include "EnginePCH.h"
#include "Editor/BlueprintEditor/BlueprintEditor.h"
#include "Editor/BlueprintEditor/BlueprintEditorTab.h"

#include <algorithm>

#include "Asset/AssetUtils.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Core/EngineLog.h"
#include "Core/Misc/Paths.h"
#include "Engine/Blueprint.h"
#include "Editor/HitoriEd/LevelUndoHistory.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Math/BoxSphereBounds.h"
#include "UObject/UObjectHash.h"

#include "Serialization/MemoryWriter.h"
#include "Serialization/StructuredArchive.h"
#include "Serialization/StructuredArchiveSlots.h"
#include "Serialization/Formatter/JsonArchiveOutputFormatter.h"

namespace
{
	// 에디터가 받은 경로("Assets/Blueprints/BP_Door.bp" 또는 Assets 기준 키)를 실제 파일 경로로 바꾼다.
	std::filesystem::path ResolveFile(const FString& AssetPath)
	{
		const FString Resolved = FAssetUtils::ResolveAssetFilePath(AssetPath);
		return FPaths::FromUtf8(Resolved.empty() ? AssetPath : Resolved);
	}
}

FBlueprintEditor::FBlueprintEditor(UEditorEngine& InEditorEngine, FEditorManager& InEditorManager, const FString& InAssetPath)
	: FAssetEditor(InEditorEngine, InEditorManager, InAssetPath)
{
}

bool FBlueprintEditor::InitializeResources()
{
	const std::filesystem::path File = ResolveFile(GetAssetPath());

	// 엔진 시작 뒤에 만든 BP는 아직 등록되지 않았을 수 있으므로 여기서 읽는다.
	BlueprintClass = FindClass(FPaths::ToUtf8(File.stem()));
	if (!BlueprintClass || !FBlueprint::IsBlueprintClass(BlueprintClass))
	{
		FString Error;
		BlueprintClass = FBlueprint::LoadBlueprint(File, &Error);
		if (!BlueprintClass)
		{
			HTR_LOG(Warning, "Blueprint editor: cannot open {} ({})", GetAssetPath(), Error);
			return false;
		}
	}

	if (!FAssetEditor::InitializeResources())
		return false;

	// 미리보기 월드에 BP 클래스로 액터를 만든다. SpawnActor가 BP 기본값을 적용해 준다.
	Template = PreviewScene.AddActor(BlueprintClass);
	if (!Template)
	{
		HTR_LOG(Warning, "Blueprint editor: cannot spawn {}", BlueprintClass->Name);
		return false;
	}

	SerializeTemplate(SavedBytes);
	bDirty = false;
	return true;
}

TUniquePtr<IEditorTab> FBlueprintEditor::CreateTab()
{
	return MakeUnique<FBlueprintEditorTab>(*this);
}

void FBlueprintEditor::ShutdownResources()
{
	Template = nullptr;
	SelectedComponent = nullptr;
	FAssetEditor::ShutdownResources();
}

bool FBlueprintEditor::SerializeTemplate(TArray<uint8>& OutBytes) const
{
	OutBytes.Empty();
	if (!Template)
		return false;

	FMemoryWriter Writer(OutBytes);
	Writer.SetIsPersistent(true);
	{
		FJsonArchiveOutputFormatter Formatter(Writer);
		FStructuredArchive Ar(Formatter);
		FStructuredArchive::FRecord Root = Ar.Open().EnterRecord();
		Template->Serialize(Root.EnterRecord("Defaults"));
		Ar.Close();
	}
	return !Writer.IsError();
}

bool FBlueprintEditor::Save()
{
	if (!Template || !BlueprintClass)
		return false;

	FString Error;
	const std::filesystem::path File = ResolveFile(GetAssetPath());
	if (!FBlueprint::SaveBlueprint(File, BlueprintClass->Super, Template, Error))
	{
		HTR_LOG(Error, "Blueprint: failed to save {} ({})", GetAssetPath(), Error);
		const std::wstring Message = FPaths::FromUtf8("Failed to save blueprint.\n" + Error).wstring();
		MessageBoxW(GetActiveWindow(), Message.c_str(), L"Save Blueprint", MB_OK | MB_ICONERROR);
		return false;
	}

	SerializeTemplate(SavedBytes);
	bDirty = false;
	// 저장하면 레벨에 놓인 이 BP 액터들도 새 기본값으로 갱신되므로 레벨 실행 취소 기록에 남긴다.
	FLevelUndoHistory::MarkDirty();
	HTR_LOG(Info, "Blueprint saved: {}", GetAssetPath());
	return true;
}

bool FBlueprintEditor::ConfirmClose()
{
	if (!bDirty)
		return true;

	const std::wstring Message = FPaths::FromUtf8("Save changes to " + BlueprintClass->Name + " before closing?").wstring();
	const int Result = MessageBoxW(GetActiveWindow(), Message.c_str(), L"Blueprint", MB_YESNOCANCEL | MB_ICONQUESTION);
	if (Result == IDCANCEL)
		return false;
	if (Result == IDYES)
		return Save();
	return true;   // 저장하지 않고 닫는다
}

void FBlueprintEditor::DeleteComponent(UActorComponent* Component)
{
	if (!Template || !Component || Component == Template->GetRootComponent())
		return;
	if (SelectedComponent == Component)
		SelectedComponent = nullptr;
	Template->DestroyComponent(Component);
	NoteEdited();
}

void FBlueprintEditor::FocusTemplate()
{
	if (!ViewportClient || !Template)
		return;

	bool bHasBox = false;
	FVector Min, Max;
	for (UActorComponent* Component : Template->GetComponents())
	{
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
		if (!Primitive)
			continue;
		const FBox Box = Primitive->CalcBounds();
		const FVector Size = Box.Max - Box.Min;
		if (Size.X <= 1e-4f && Size.Y <= 1e-4f && Size.Z <= 1e-4f)
			continue;
		if (!bHasBox)
		{
			Min = Box.Min;
			Max = Box.Max;
			bHasBox = true;
			continue;
		}
		Min = FVector(std::min(Min.X, Box.Min.X), std::min(Min.Y, Box.Min.Y), std::min(Min.Z, Box.Min.Z));
		Max = FVector(std::max(Max.X, Box.Max.X), std::max(Max.Y, Box.Max.Y), std::max(Max.Z, Box.Max.Z));
	}

	FBoxSphereBounds Bounds;
	if (bHasBox)
	{
		Bounds.Origin = (Min + Max) * 0.5f;
		Bounds.BoxExtent = (Max - Min) * 0.5f;
		Bounds.SphereRadius = Bounds.BoxExtent.Size();
	}
	else
	{
		// 메시가 없는 BP(GameMode 등)는 원점 근처를 본다.
		Bounds.Origin = Template->GetActorLocation();
		Bounds.BoxExtent = FVector(0.5f, 0.5f, 0.5f);
		Bounds.SphereRadius = 1.0f;
	}
	ViewportClient->FocusOnBounds(Bounds);
}

void FBlueprintEditor::SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input)
{
	ViewportWidth = Width;
	ViewportHeight = Height;
	bViewportVisible = Width > 0 && Height > 0;
	CameraInput = Input;
}

void FBlueprintEditor::Tick(float DeltaTime)
{
	if (ViewportClient && ViewportClient->GetWorld() && bViewportVisible)
	{
		FViewportCameraState Camera{
			.Location = ViewportClient->GetViewLocation(),
			.Rotation = ViewportClient->GetViewRotation(),
			.OrthoWidth = ViewportClient->GetOrthoWidth(),
			.ViewWidth = static_cast<float>(ViewportWidth),
			.bPerspective = true
		};
		ApplyViewportCameraMovement(Camera, CameraInput, DeltaTime, 5.0f, 0.2f);
		ViewportClient->SetViewLocation(Camera.Location);
		ViewportClient->SetViewRotation(Camera.Rotation);
	}
	CameraInput = {};

	// 편집이 끝났으면 저장된 상태와 비교해 * 표시를 정한다. 액터 하나라 가볍다.
	if (bCheckDirty && !ImGui::IsAnyItemActive())
	{
		bCheckDirty = false;
		TArray<uint8> Current;
		if (SerializeTemplate(Current))
			bDirty = !(Current == SavedBytes);
	}

	FAssetEditor::Tick(DeltaTime);
}

void FBlueprintEditor::OnRenderPreview()
{
	const bool bRender = bViewportVisible;
	bViewportVisible = false;
	if (!bRender || !ViewportClient || !ViewportClient->GetWorld() || !Viewport)
		return;

	Viewport->UpdateViewportRHI(false, ViewportWidth, ViewportHeight);
	if (!Viewport->GetViewTargets().IsValidFor(ViewportWidth, ViewportHeight))
		return;

	if (bInitialFocusPending)
	{
		FocusTemplate();
		bInitialFocusPending = false;
	}
	Viewport->Draw();
}
