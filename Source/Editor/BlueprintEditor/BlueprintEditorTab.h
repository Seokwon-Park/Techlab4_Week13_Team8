#pragma once

#include "Editor/AssetEditor/AssetEditorTab.h"
#include "Editor/EditorUI/EditorPanel.h"
#include "Core/EngineString.h"
#include "Containers/Array.h"

class FBlueprintEditor;

// Blueprint 에디터 패널의 공통 부분 (창 이름, 도킹 클래스, 분리 여부). StaticMesh 에디터 패널과 같은 방식이다.
class FBlueprintEditorPanel : public IEditorPanel
{
public:
	FBlueprintEditorPanel(FBlueprintEditor& InEditor, const char* InName);

	bool Init() override { return true; }
	void Tick(float DeltaTime) override {}
	void OnRender() override;

	const char* GetPanelName() const override { return PanelName.c_str(); }
	const char* GetWindowName() const { return WindowName.c_str(); }
	void SetDockClass(ImGuiID InClassId) { DockClassId = InClassId; }
	bool IsDetached(ImGuiID DockSpaceId) const;

protected:
	virtual void DrawContent() = 0;
	FBlueprintEditor& Editor;

private:
	FString PanelName;
	FString WindowName;
	ImGuiID DockClassId = 0;
};

// 왼쪽 위: 컴포넌트 트리와 [+ Add]
class FBlueprintComponentsPanel : public FBlueprintEditorPanel
{
public:
	explicit FBlueprintComponentsPanel(FBlueprintEditor& InEditor) : FBlueprintEditorPanel(InEditor, "Components") {}

protected:
	void DrawContent() override;

private:
	void DrawSceneNode(class USceneComponent* Component, class USceneComponent* Root);
	void DrawRowLabel(class UActorComponent* Component, const ImVec2& Min, const ImVec2& Max);

	char AddSearch[128] = {};
	class UActorComponent* PendingDelete = nullptr;
	class UActorComponent* RenamingComponent = nullptr;
	char RenameBuffer[256] = {};
	bool bFocusRename = false;
};

// 가운데: 미리보기 뷰포트
class FBlueprintViewportPanel : public FBlueprintEditorPanel
{
public:
	explicit FBlueprintViewportPanel(FBlueprintEditor& InEditor) : FBlueprintEditorPanel(InEditor, "Viewport") {}

protected:
	void DrawContent() override;
};

// 왼쪽 아래: 고른 컴포넌트(또는 액터 자신)의 기본값
class FBlueprintDetailsPanel : public FBlueprintEditorPanel
{
public:
	explicit FBlueprintDetailsPanel(FBlueprintEditor& InEditor) : FBlueprintEditorPanel(InEditor, "Details") {}

protected:
	void DrawContent() override;
};

class FBlueprintEditorTab : public FAssetEditorTab
{
public:
	explicit FBlueprintEditorTab(FBlueprintEditor& InEditor);

	void DrawMenuBar() override;
	void OnRender(bool bActive) override;

private:
	void BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size);

	FBlueprintEditor& BlueprintEditor;
	TArray<TUniquePtr<FBlueprintEditorPanel>> Panels;
	ImGuiID DockSpaceId = 0;
	ImGuiID PanelClassId = 0;
	bool bLayoutInitialized = false;
};
