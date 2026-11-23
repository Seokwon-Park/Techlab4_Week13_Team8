#pragma once

#include "Editor/AssetEditor/AssetEditorPanel.h"
#include "Editor/PhysicsAssetEditor/PhysicsAssetEditor.h"
#include "imgui_node_editor.h"
#include "Containers/Array.h"

// UE PhAT의 Graph 패널 (UPhysicsAssetGraph). 루트 Body와 그 Body에 붙은 Constraint, 반대편 Body를 보여 준다
// 클릭: 에디터 선택만 바꾼다 (루트 유지). 더블클릭: 그 Body를 루트로 다시 그린다
// 트리·뷰포트에서 선택이 바뀌면 그 Body를 루트로 삼는다
class FPhysicsAssetGraphPanel : public TAssetEditorPanel<FPhysicsAssetEditor>
{
public:
	explicit FPhysicsAssetGraphPanel(FPhysicsAssetEditor& InEditor)
		: TAssetEditorPanel<FPhysicsAssetEditor>(InEditor, "Graph", "PhysicsAssetPanel")
	{
	}
	~FPhysicsAssetGraphPanel() override;

protected:
	void DrawContent() override;

private:
	struct FGraphNode
	{
		int32 NodeId = 0;
		EPhysicsAssetSelection Type = EPhysicsAssetSelection::None;
		int32 Index = INDEX_NONE;   // Body 또는 Constraint 인덱스
	};

	// 에디터 선택이 그래프 밖(트리·뷰포트)에서 바뀌었으면 루트를 옮긴다
	void SyncRootWithEditorSelection();
	void SetRootBody(int32 BodyIndex);
	bool IsConstraintInGraph(int32 ConstraintIndex) const;

	void DrawBodyNode(int32 NodeId, int32 BodyIndex, int32 InputPinId, int32 OutputPinId, const ImVec2& Position);
	void DrawConstraintNode(int32 NodeId, int32 ConstraintIndex, int32 InputPinId, int32 OutputPinId, const ImVec2& Position);

	// 노드에 쓰는 글자. 그리기와 크기 계산이 같은 문자열을 쓴다
	FString GetBoneDisplayName(FName BoneName) const;
	FString GetBodyName(int32 BodyIndex) const;
	FString GetShapesLabel(int32 BodyIndex) const;
	FString GetConstraintLabel(int32 ConstraintIndex) const;

	// 노드 편집기 선택 ↔ 에디터 선택
	void PushEditorSelectionToGraph();
	void PullGraphSelectionToEditor();
	const FGraphNode* FindNode(int32 NodeId) const;
	int32 FindNodeId(EPhysicsAssetSelection Type, int32 Index) const;

	ax::NodeEditor::EditorContext* Context = nullptr;
	TArray<FGraphNode> Nodes;            // 이번 프레임에 그린 노드
	int32 RootBody = INDEX_NONE;
	bool bLayoutDirty = true;            // 루트가 바뀌면 노드 위치를 다시 잡고 화면을 맞춘다

	// 마지막으로 맞춘 에디터 선택. 이와 다르면 그래프 밖에서 바뀐 것
	EPhysicsAssetSelection SyncedType = EPhysicsAssetSelection::None;
	int32 SyncedIndex = INDEX_NONE;
	int32 SyncedGraphNodeId = 0;         // 노드 편집기에서 선택된 노드 (클릭 감지용)
	bool bPushSelection = true;          // 에디터 선택을 노드 편집기 선택에 반영해야 함
};
