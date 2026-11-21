#pragma once

#include "Editor/AssetEditor/AssetEditor.h"
#include "Camera/ViewportCameraMovement.h"
#include "Containers/Array.h"

class UClass;
class AActor;
class UActorComponent;

// 데이터 Blueprint 에디터. UE의 Blueprint 에디터에서 Components / Viewport / Details 부분에 해당한다.
// 미리보기 월드에 BP 클래스로 액터(템플릿)를 하나 만들고, 그 액터를 고친 뒤 Save하면
// 템플릿의 지금 상태가 BP 기본값이 된다 (FBlueprint::SaveBlueprint).
class FBlueprintEditor : public FAssetEditor
{
public:
	FBlueprintEditor(class UEditorEngine& InEditorEngine, class FEditorManager& InEditorManager, const FString& InAssetPath);

	void Tick(float DeltaTime) override;
	void OnRenderPreview() override;

	const char* GetEditorTypeName() const override { return "Blueprint"; }
	bool IsDirty() const override { return bDirty; }
	bool ConfirmClose() override;

	// 템플릿 상태를 .bp에 쓴다. 실패하면 로그와 메시지 창을 띄운다.
	bool Save();

	// 템플릿 액터 전체가 화면에 들어오게 카메라를 옮긴다.
	void FocusTemplate();
	// 뷰포트 패널이 매 프레임 크기와 카메라 입력을 넘긴다. 보이지 않으면 0이다.
	void SetViewportState(uint32 Width, uint32 Height, const FViewportCameraInput& Input);

	UClass* GetBlueprintClass() const { return BlueprintClass; }
	AActor* GetTemplate() const { return Template; }

	// 고르고 있는 컴포넌트. nullptr이면 액터 자신(Self)이다.
	UActorComponent* GetSelectedComponent() const { return SelectedComponent; }
	void SelectComponent(UActorComponent* Component) { SelectedComponent = Component; }
	// 컴포넌트를 지운다. 루트는 지울 수 없다. 패널을 다 그린 뒤 부른다.
	void DeleteComponent(UActorComponent* Component);

	// 패널이 값을 바꿨을 수 있을 때 부른다. 다음 Tick에서 저장된 상태와 비교해 바뀌었는지 가린다.
	void NoteEdited() { bCheckDirty = true; }

protected:
	TUniquePtr<class IEditorTab> CreateTab() override;
	bool InitializeResources() override;
	void ShutdownResources() override;

private:
	bool SerializeTemplate(TArray<uint8>& OutBytes) const;

	UClass* BlueprintClass = nullptr;
	AActor* Template = nullptr;
	UActorComponent* SelectedComponent = nullptr;

	// 마지막으로 저장한(또는 연) 상태. 지금 상태와 비교해 * 표시를 정한다.
	TArray<uint8> SavedBytes;
	bool bDirty = false;
	bool bCheckDirty = false;

	FViewportCameraInput CameraInput;
	uint32 ViewportWidth = 0;
	uint32 ViewportHeight = 0;
	bool bViewportVisible = false;
	bool bInitialFocusPending = true;
};
