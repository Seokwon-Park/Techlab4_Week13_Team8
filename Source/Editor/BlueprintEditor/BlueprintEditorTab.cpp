#include "EnginePCH.h"
#include "Editor/BlueprintEditor/BlueprintEditorTab.h"
#include "Editor/BlueprintEditor/BlueprintEditor.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Core/EngineLog.h"
#include "Editor/Details/PropertyEditor.h"
#include "Editor/EditorUI/ComponentTypeInfo.h"
#include "Editor/EditorUI/EditorIcons.h"
#include "Editor/Viewports/EditorViewportToolbar.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"

// ---------------- 패널 공통 ----------------

FBlueprintEditorPanel::FBlueprintEditorPanel(FBlueprintEditor& InEditor, const char* InName)
	: Editor(InEditor)
	, PanelName(InName)
	, WindowName(PanelName + "###BlueprintPanel:" + InName + ":" + InEditor.GetAssetPath())
{
}

void FBlueprintEditorPanel::OnRender()
{
	// 같은 BP 에디터의 패널끼리만 도킹한다.
	ImGuiWindowClass WindowClass;
	WindowClass.ClassId = DockClassId;
	WindowClass.DockingAllowUnclassed = false;
	WindowClass.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
	ImGui::SetNextWindowClass(&WindowClass);
	ImGui::SetNextWindowSize(ImVec2(480.0f, 320.0f), ImGuiCond_FirstUseEver);

	if (EditorPanelUI::BeginPanel(GetWindowName(), GetOpenFlag()))
		DrawContent();
	ImGui::End();
}

bool FBlueprintEditorPanel::IsDetached(ImGuiID InDockSpaceId) const
{
	const ImGuiWindow* Window = ImGui::FindWindowByName(GetWindowName());
	return Window && (!Window->DockNode || ImGui::DockNodeGetRootNode(Window->DockNode)->ID != InDockSpaceId);
}

// ---------------- Components ----------------

void FBlueprintComponentsPanel::DrawContent()
{
	AActor* Template = Editor.GetTemplate();
	if (!Template)
		return;

	const float S = EditorIcons::GetScale();

	// [+ Add] : Details 패널과 같은 분류·검색 메뉴
	const float AddH = ImGui::GetFrameHeight() + 2.0f * S;
	const ImVec2 AddMin = ImGui::GetCursorScreenPos();
	const float AddW = EditorIcons::CalcButtonWidth(true, "Add", false, AddH);
	ImDrawList* DL = ImGui::GetWindowDrawList();
	DL->AddRectFilled(AddMin, ImVec2(AddMin.x + AddW, AddMin.y + AddH), ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f * S);
	DL->AddRect(AddMin, ImVec2(AddMin.x + AddW, AddMin.y + AddH), ImGui::GetColorU32(ImGuiCol_Border), 4.0f * S);
	if (EditorIcons::Button("##AddComponent", EEditorIcon::Plus, EditorIcons::PlayGreen, "Add", false, false, AddH))
	{
		AddSearch[0] = '\0';
		ImGui::OpenPopup("BlueprintAddComponent");
	}
	ImGui::SetItemTooltip("Add a component to this blueprint");
	ImGui::SetNextWindowSize(ImVec2(300.0f * S, 0.0f), ImGuiCond_Appearing);
	if (ImGui::BeginPopup("BlueprintAddComponent"))
	{
		if (UActorComponent* Added = FPropertyEditor::DrawAddComponentMenu(Template, AddSearch, sizeof(AddSearch)))
		{
			Editor.SelectComponent(Added);
			Editor.NoteEdited();
		}
		ImGui::EndPopup();
	}
	ImGui::Spacing();

	// 맨 위 줄은 액터 자신(Self). 누르면 액터 기본값을 고친다.
	{
		EEditorIcon Icon;
		ImU32 Color;
		ComponentTypeInfo::GetActorIcon(Template, Icon, Color);
		const FString Label = Editor.GetBlueprintClass()->Name + " (Self)";
		const float H = ImGui::GetTextLineHeight() + 6.0f * S;
		const ImVec2 Min = ImGui::GetCursorScreenPos();
		if (ImGui::Selectable("##Self", Editor.GetSelectedComponent() == nullptr, 0, ImVec2(0.0f, H)))
			Editor.SelectComponent(nullptr);
		EditorIcons::Draw(DL, Icon, ImVec2(Min.x + 11.0f * S, Min.y + H * 0.5f), 14.0f * S, Color);
		DL->AddText(ImVec2(Min.x + 24.0f * S, Min.y + (H - ImGui::GetFontSize()) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), Label.c_str());
	}

	ImGui::Indent(12.0f * S);
	if (USceneComponent* Root = Template->GetRootComponent())
		DrawSceneNode(Root, Root);

	// 위치가 없는 컴포넌트(이동 등)
	for (UActorComponent* Component : Template->GetComponents())
	{
		if (!Component || Cast<USceneComponent>(Component))
			continue;
		ImGui::PushID(Component);
		const bool bSelected = Editor.GetSelectedComponent() == Component;
		ImGui::TreeNodeEx("##Node", ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen |
			ImGuiTreeNodeFlags_SpanAvailWidth | (bSelected ? ImGuiTreeNodeFlags_Selected : 0), "%s", "");
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		if (ImGui::IsItemClicked())
			Editor.SelectComponent(Component);
		if (ImGui::BeginPopupContextItem("##Context"))
		{
			if (ImGui::MenuItem("Delete"))
				PendingDelete = Component;
			ImGui::EndPopup();
		}
		DrawRowLabel(Component, Min, Max);
		ImGui::PopID();
	}
	ImGui::Unindent(12.0f * S);

	// 그리기가 끝난 뒤 지운다.
	if (PendingDelete)
	{
		if (RenamingComponent == PendingDelete)
			RenamingComponent = nullptr;
		Editor.DeleteComponent(PendingDelete);
		PendingDelete = nullptr;
	}
}

void FBlueprintComponentsPanel::DrawRowLabel(UActorComponent* Component, const ImVec2& Min, const ImVec2& Max)
{
	const float S = EditorIcons::GetScale();
	const float IconSize = 14.0f * S;
	const float MidY = (Min.y + Max.y) * 0.5f;
	float X = Min.x + ImGui::GetTreeNodeToLabelSpacing();
	ImDrawList* DL = ImGui::GetWindowDrawList();

	EEditorIcon Icon;
	ImU32 Color;
	ComponentTypeInfo::GetComponentIcon(Component->GetClass(), Icon, Color);
	EditorIcons::Draw(DL, Icon, ImVec2(X + IconSize * 0.5f, MidY), IconSize, Color);
	X += IconSize + 6.0f * S;

	const float TextY = MidY - ImGui::GetFontSize() * 0.5f;
	const FString Name = Component->GetName();
	DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_Text), Name.c_str());
	X += ImGui::CalcTextSize(Name.c_str()).x + 5.0f * S;
	const FString Kind = "(" + ComponentTypeInfo::GetComponentDisplayName(Component->GetClass()->Name) + ")";
	DL->AddText(ImVec2(X, TextY), ImGui::GetColorU32(ImGuiCol_TextDisabled), Kind.c_str());
}

void FBlueprintComponentsPanel::DrawSceneNode(USceneComponent* Component, USceneComponent* Root)
{
	if (!Component || Component->IsHiddenInDetails())
		return;

	TArray<USceneComponent*> Children;
	for (USceneComponent* Child : Component->GetAttachChildren())
		if (Child && !Child->IsHiddenInDetails() && Child->GetOwner() == Component->GetOwner())
			Children.Add(Child);

	const bool bSelected = Editor.GetSelectedComponent() == Component;
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (bSelected) Flags |= ImGuiTreeNodeFlags_Selected;
	if (Children.IsEmpty()) Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

	ImGui::PushID(Component);
	const bool bOpen = ImGui::TreeNodeEx("##Node", Flags, "%s", "");
	const ImVec2 Min = ImGui::GetItemRectMin();
	const ImVec2 Max = ImGui::GetItemRectMax();
	const bool bRenaming = RenamingComponent == Component;

	if (!bRenaming)
	{
		if (ImGui::IsItemClicked())
			Editor.SelectComponent(Component);
		// 더블클릭으로 이름 바꾸기 (루트 제외, 레벨 Details와 같다)
		if (Component != Root && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			RenamingComponent = Component;
			strncpy_s(RenameBuffer, sizeof(RenameBuffer), Component->GetName().c_str(), _TRUNCATE);
			bFocusRename = true;
		}
		if (ImGui::BeginPopupContextItem("##Context"))
		{
			if (ImGui::MenuItem("Delete", nullptr, false, Component != Root))
				PendingDelete = Component;
			ImGui::EndPopup();
		}
		DrawRowLabel(Component, Min, Max);
	}
	else
	{
		const float S = EditorIcons::GetScale();
		ImGui::SameLine(0.0f, 20.0f * S);
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (bFocusRename)
		{
			ImGui::SetKeyboardFocusHere();
			bFocusRename = false;
		}
		const bool bEnter = ImGui::InputText("##Rename", RenameBuffer, sizeof(RenameBuffer),
			ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
		if (bEnter || ImGui::IsItemDeactivated())
		{
			const FName NewName(RenameBuffer);
			AActor* Owner = Component->GetOwner();
			if (RenameBuffer[0] != '\0' && NewName != Component->GetFName())
			{
				if (Owner && Owner->FindComponentByName(NewName))
					HTR_LOG(Warning, "A component named '{}' already exists in this blueprint", RenameBuffer);
				else
				{
					Component->SetName(NewName);
					Editor.NoteEdited();
				}
			}
			RenamingComponent = nullptr;
		}
	}

	if (bOpen && !Children.IsEmpty())
	{
		for (USceneComponent* Child : Children)
			DrawSceneNode(Child, Root);
		ImGui::TreePop();
	}
	ImGui::PopID();
}

// ---------------- Viewport ----------------

void FBlueprintViewportPanel::DrawContent()
{
	FEditorViewportClient* Client = Editor.GetViewportClient();
	const ImVec2 Size = ImGui::GetContentRegionAvail();
	if (!Client || Size.x < 1.0f || Size.y < 1.0f)
		return;

	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	const ImVec2 ViewMax(Origin.x + Size.x, Origin.y + Size.y);
	if (FTexture2D* Target = Client->Viewport ? Client->Viewport->GetRenderTargetTexture() : nullptr; Target && Target->GetSRV())
		ImGui::GetWindowDrawList()->AddImage(Target->GetSRV(), Origin, ViewMax);

	const bool bToolbarBusy = EditorViewportUI::DrawAssetViewportToolbar(Client, Origin, ViewMax);

	ImGui::SetCursorScreenPos(Origin);
	ImGui::InvisibleButton("##BlueprintViewport", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool bHovered = ImGui::IsItemHovered() && !bToolbarBusy;
	if (bHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		ImGui::SetWindowFocus();

	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
	if (bFocused && bHovered)
		EditorViewportUI::HandleViewTypeShortcuts(Client);
	if (bFocused && bHovered && ImGui::IsKeyPressed(ImGuiKey_F, false))
		Editor.FocusTemplate();

	const ImGuiIO& IO = ImGui::GetIO();
	FViewportCameraInput Input
	{
		.bCaptured   = bFocused && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Right),
		.bAllowWheel = bHovered,
		.bForward    = ImGui::IsKeyDown(ImGuiKey_W),
		.bBackward   = ImGui::IsKeyDown(ImGuiKey_S),
		.bRight      = ImGui::IsKeyDown(ImGuiKey_D),
		.bLeft       = ImGui::IsKeyDown(ImGuiKey_A),
		.bUp         = ImGui::IsKeyDown(ImGuiKey_E),
		.bDown       = ImGui::IsKeyDown(ImGuiKey_Q),
		.MouseX      = IO.MouseDelta.x,
		.MouseY      = IO.MouseDelta.y,
		.Wheel       = IO.MouseWheel * 120.0f,
	};
	Editor.SetViewportState(static_cast<uint32>(Size.x), static_cast<uint32>(Size.y), Input);
}

// ---------------- Details ----------------

void FBlueprintDetailsPanel::DrawContent()
{
	AActor* Template = Editor.GetTemplate();
	UClass* BlueprintClass = Editor.GetBlueprintClass();
	if (!Template || !BlueprintClass)
		return;

	ImGui::TextDisabled("Parent Class");
	ImGui::SameLine();
	ImGui::TextUnformatted(BlueprintClass->Super ? BlueprintClass->Super->Name.c_str() : "None");
	ImGui::Separator();

	// 고른 컴포넌트가 지워졌을 수 있으니 템플릿에 아직 있는지 확인한다.
	UActorComponent* Selected = Editor.GetSelectedComponent();
	if (Selected && Template->GetComponents().Find(Selected) == INDEX_NONE)
	{
		Editor.SelectComponent(nullptr);
		Selected = nullptr;
	}

	if (Selected)
	{
		ImGui::PushID(Selected);
		FPropertyEditor::DrawObjectProperties(Selected);
		FPropertyEditor::DrawComponentExtras(Selected);
		ImGui::PopID();
	}
	else
	{
		ImGui::PushID(Template);
		FPropertyEditor::DrawObjectProperties(Template);
		FPropertyEditor::DrawActorExtras(Template);
		ImGui::PopID();
	}

	// 이 패널에서 값이 바뀌었거나 마우스를 놓았으면 저장 표시(*)를 다시 계산한다.
	const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
	const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
	if ((bFocused && ImGui::GetCurrentContext()->ActiveIdHasBeenEditedThisFrame) ||
		((bFocused || bHovered) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)))
		Editor.NoteEdited();
}

// ---------------- Tab ----------------

FBlueprintEditorTab::FBlueprintEditorTab(FBlueprintEditor& InEditor)
	: FAssetEditorTab(InEditor)
	, BlueprintEditor(InEditor)
{
	const ImGuiID AssetId = ImHashStr(InEditor.GetAssetPath().c_str());
	DockSpaceId = ImHashStr("BlueprintDockSpace", 0, AssetId);
	PanelClassId = ImHashStr("BlueprintPanelClass", 0, AssetId);

	Panels.Add(MakeUnique<FBlueprintComponentsPanel>(InEditor));
	Panels.Add(MakeUnique<FBlueprintDetailsPanel>(InEditor));
	Panels.Add(MakeUnique<FBlueprintViewportPanel>(InEditor));
	for (auto& Panel : Panels)
	{
		Panel->SetDockClass(PanelClassId);
		Panel->Init();
	}
}

void FBlueprintEditorTab::DrawMenuBar()
{
	if (ImGui::BeginMenu("File"))
	{
		if (ImGui::MenuItem("Save", "Ctrl+S", false, BlueprintEditor.IsDirty()))
			BlueprintEditor.Save();
		ImGui::Separator();
		if (ImGui::MenuItem("Close Blueprint"))
			BlueprintEditor.Close();
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		for (auto& Panel : Panels)
			if (ImGui::MenuItem(Panel->GetPanelName(), nullptr, Panel->IsOpen()))
				Panel->SetOpen(!Panel->IsOpen());
		ImGui::EndMenu();
	}
}

void FBlueprintEditorTab::BuildDefaultLayout(ImGuiID InDockSpaceId, const ImVec2& Size)
{
	// UE5 Blueprint 에디터처럼 왼쪽 위 Components, 왼쪽 아래 Details, 나머지 Viewport
	ImGui::DockBuilderAddNode(InDockSpaceId, ImGuiDockNodeFlags_DockSpace);
	ImGui::DockBuilderSetNodeSize(InDockSpaceId, Size);
	ImGuiID ViewportId = InDockSpaceId;
	ImGuiID LeftId = ImGui::DockBuilderSplitNode(ViewportId, ImGuiDir_Left, 0.28f, nullptr, &ViewportId);
	ImGuiID DetailsId = LeftId;
	const ImGuiID ComponentsId = ImGui::DockBuilderSplitNode(DetailsId, ImGuiDir_Up, 0.38f, nullptr, &DetailsId);

	ImGui::DockBuilderDockWindow(Panels[0]->GetWindowName(), ComponentsId);
	ImGui::DockBuilderDockWindow(Panels[1]->GetWindowName(), DetailsId);
	ImGui::DockBuilderDockWindow(Panels[2]->GetWindowName(), ViewportId);
	ImGui::DockBuilderFinish(InDockSpaceId);
}

void FBlueprintEditorTab::OnRender(bool bActive)
{
	BlueprintEditor.SetViewportState(0, 0, {});
	if (BlueprintEditor.IsCloseRequested())
		return;

	// Ctrl+S: 이 탭이 앞에 있을 때 BP를 저장한다 (레벨 저장 단축키는 레벨 탭에서만 동작한다).
	if (bActive && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && BlueprintEditor.IsDirty())
		BlueprintEditor.Save();

	if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
	{
		const ImVec2 Size = ImGui::GetContentRegionAvail();
		if (bActive && Size.x > 0.0f && Size.y > 0.0f && (!bLayoutInitialized || !ImGui::DockBuilderGetNode(DockSpaceId)))
		{
			ImGui::DockBuilderRemoveNode(DockSpaceId);
			BuildDefaultLayout(DockSpaceId, Size);
			bLayoutInitialized = true;
		}

		ImGuiWindowClass WindowClass;
		WindowClass.ClassId = PanelClassId;
		WindowClass.DockingAllowUnclassed = false;
		const ImGuiDockNodeFlags Flags = bActive ? ImGuiDockNodeFlags_None : ImGuiDockNodeFlags_KeepAliveOnly;
		if (bActive || ImGui::DockBuilderGetNode(DockSpaceId))
			EditorPanelUI::DockSpace(DockSpaceId, ImVec2(0.0f, 0.0f), Flags, &WindowClass);
	}

	for (auto& Panel : Panels)
		if (Panel->IsOpen() && (bActive || Panel->IsDetached(DockSpaceId)))
			Panel->OnRender();
}
