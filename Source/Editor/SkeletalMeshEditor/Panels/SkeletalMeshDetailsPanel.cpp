#include "EnginePCH.h"
#include "SkeletalMeshDetailsPanel.h"
#include "Editor/SkeletalMeshEditor/SkeletalMeshEditor.h"
#include "Math/Transform.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsAssetFile.h"
#include "Physics/PhysicsAssetUtils.h"
#include "Render/SkeletalMesh.h"
#include "UObject/UObjectGlobals.h"
#include <filesystem>
#include <charconv>
#include <sstream>

namespace
{
	// Assets/Physics/<메시 이름>.physicsasset
	std::filesystem::path DebugPhysicsAssetPath(const FString& MeshAssetPath)
	{
		const FString Stem = std::filesystem::path(MeshAssetPath).stem().string();
		return std::filesystem::path("Assets/Physics") / (Stem + ".physicsasset");
	}

	// 3단계 확인용: 바디 2개, 관절 1개, 충돌 끄기 1쌍을 손으로 만든 에셋
	UPhysicsAsset* MakeTestPhysicsAsset()
	{
		UPhysicsAsset* Asset = NewObject<UPhysicsAsset>();
		for (const char* Bone : { "pelvis", "spine_01" })
		{
			USkeletalBodySetup* Setup = NewObject<USkeletalBodySetup>(Asset);
			Setup->BoneName = FName(Bone);
			FKSphylElem Capsule;
			Capsule.Radius = 15.f;
			Capsule.Length = 20.f;
			Setup->AggGeom.SphylElems.Add(Capsule);
			Asset->SkeletalBodySetups.Add(Setup);
		}

		UPhysicsConstraintTemplate* Joint = NewObject<UPhysicsConstraintTemplate>(Asset);
		Joint->DefaultInstance.ConstraintBone1 = FName("spine_01");
		Joint->DefaultInstance.ConstraintBone2 = FName("pelvis");
		Joint->DefaultInstance.Pos2 = FVector(0, 0, 10);
		Asset->ConstraintSetup.Add(Joint);

		Asset->DisableCollision(0, 1);
		Asset->UpdateBodySetupIndexMap();
		return Asset;
	}

	// 저장 → 새 객체로 다시 읽기 → 개수 비교
	FString SaveAndReload(UPhysicsAsset& Asset, const std::filesystem::path& Path)
	{
		FString Error;
		if (!PhysicsAssetFile::Write(Path, Asset, Error)) return "Write failed: " + Error;

		UPhysicsAsset* Loaded = NewObject<UPhysicsAsset>();
		if (!PhysicsAssetFile::Read(Path, *Loaded, Error)) return "Read failed: " + Error;

		const bool bSame = Loaded->SkeletalBodySetups.Num() == Asset.SkeletalBodySetups.Num()
			&& Loaded->ConstraintSetup.Num() == Asset.ConstraintSetup.Num()
			&& Loaded->CollisionDisableTable.Num() == Asset.CollisionDisableTable.Num();
		return std::format("{} {} (bodies {}, constraints {}, disabled pairs {})",
			bSame ? "OK" : "MISMATCH", Path.generic_string(),
			Loaded->SkeletalBodySetups.Num(), Loaded->ConstraintSetup.Num(),
			Loaded->CollisionDisableTable.Num());
	}
}

FSkeletalMeshDetailsPanel::FSkeletalMeshDetailsPanel(FSkeletalMeshEditor& InEditor)
	: FSkeletalMeshEditorPanel(InEditor, "Details")
{
}

void FSkeletalMeshDetailsPanel::DrawPhysicsDebug()
{
	if (!ImGui::CollapsingHeader("Physics (debug)")) return;

	// 3단계: 직렬화 왕복
	if (ImGui::Button("Save/Load Test Asset"))
	{
		DebugMessage = SaveAndReload(*MakeTestPhysicsAsset(), "Assets/Physics/_Test.physicsasset");
		HTR_LOG(Info, "{}", DebugMessage);
	}

	// 4단계: 자동 생성
	ImGui::DragFloat("MinBoneSize", &DebugMinBoneSize, 0.1f, 0.f, 1000.f);
	if (ImGui::Button("Generate Bodies") && Editor.GetMesh())
	{
		FPhysAssetCreateParams Params;
		Params.MinBoneSize = DebugMinBoneSize;
		FString Error;
		DebugPhysicsAsset = NewObject<UPhysicsAsset>();
		if (FPhysicsAssetUtils::CreateFromSkeletalMesh(DebugPhysicsAsset, Editor.GetMesh(), Params, Error))
			DebugMessage = SaveAndReload(*DebugPhysicsAsset, DebugPhysicsAssetPath(Editor.GetAssetPath()));
		else
			DebugMessage = "Generate failed: " + (Error.empty() ? FString("no bodies (MinBoneSize too big?)") : Error);
		HTR_LOG(Info, "{}", DebugMessage);
	}

	if (!DebugMessage.empty()) ImGui::TextWrapped("%s", DebugMessage.c_str());

	if (DebugPhysicsAsset && ImGui::TreeNode("Bodies"))
	{
		for (USkeletalBodySetup* Setup : DebugPhysicsAsset->SkeletalBodySetups)
		{
			const FString Bone = Setup->BoneName.ToString();
			if (Setup->AggGeom.SphylElems.Num() > 0)
			{
				const FKSphylElem& E = Setup->AggGeom.SphylElems[0];
				ImGui::Text("%s  R %.1f  L %.1f", Bone.c_str(), E.Radius, E.Length);
			}
			else
			{
				ImGui::Text("%s  (no capsule)", Bone.c_str());
			}
		}
		ImGui::TreePop();
	}
	ImGui::Separator();
}

// 선택한 렌더 섹션의 Cloth 바인딩 입력과 적용
void FSkeletalMeshDetailsPanel::DrawClothBinding()
{
	USkeletalMesh* Mesh = Editor.GetMesh();
	if (!Mesh || Mesh->GetRenderData().LODRenderData.IsEmpty()
		|| !ImGui::CollapsingHeader("Cloth"))
	{
		return;
	}

	const auto& Sections = Mesh->GetRenderData().LODRenderData[0].Sections;
	const auto& MaterialSlots = Mesh->GetMaterialSlots();
	const FString Preview = Sections.IsValidIndex(ClothSectionIndex)
		? std::to_string(ClothSectionIndex) : "Select a section";

	// 현재 메시의 섹션 번호와 머티리얼 이름 표시
	if (ImGui::BeginCombo("Section", Preview.c_str()))
	{
		for (int32 Index = 0; Index < Sections.Num(); ++Index)
		{
			const FString Label = std::format("{} - {}",
				Index, MaterialSlots[Sections[Index].MaterialSlotIndex].Name);
			const bool bSelected = Index == ClothSectionIndex;
			if (ImGui::Selectable(Label.c_str(), bSelected) && !bSelected)
			{
				ClothSectionIndex = Index;
				ClothPinnedVertexText[0] = '\0';
				ClothBindingMessage.clear();
			}
		}
		ImGui::EndCombo();
	}

	ImGui::InputTextWithHint("Pinned vertices", "Space-separated render vertex IDs",
		ClothPinnedVertexText, sizeof(ClothPinnedVertexText));
	if (!ClothBindingMessage.empty())
	{
		ImGui::TextWrapped("%s", ClothBindingMessage.c_str());
	}

	if (!ImGui::Button("Apply Cloth Binding"))
	{
		return;
	}

	// 공백으로 구분된 원본 정점 번호 검증
	FClothSectionBinding NewBinding;
	NewBinding.SectionIndex = ClothSectionIndex;
	std::istringstream Input(ClothPinnedVertexText);
	for (FString Token; Input >> Token;)
	{
		uint32 VertexIndex = 0;
		const char* Last = Token.data() + Token.size();
		const auto [End, Error] = std::from_chars(Token.data(), Last, VertexIndex);
		if (Error != std::errc{} || End != Last)
		{
			ClothBindingMessage = "Enter unsigned vertex IDs separated by spaces.";
			return;
		}
		NewBinding.PinnedRenderVertices.Add(VertexIndex);
	}

	// 나머지 섹션을 보존한 선택 바인딩 교체
	TArray<FClothSectionBinding> Bindings;
	for (const FClothSectionBinding& Existing : Mesh->GetClothSectionBindings())
	{
		if (Existing.SectionIndex != ClothSectionIndex)
		{
			Bindings.Add(Existing);
		}
	}
	Bindings.Add(std::move(NewBinding));

	ClothBindingMessage = Mesh->SetClothSectionBindings(std::move(Bindings))
		? "Cloth binding applied. Start PIE to simulate."
		: "Select a valid section and enter vertex IDs from that section.";
}

void FSkeletalMeshDetailsPanel::DrawContent()
{
	DrawPhysicsDebug();
	DrawClothBinding();
	const int32 BoneIndex = Editor.GetSelectedBoneIndex();

	if (ImGui::Button("Reset Transform"))
	{
		Editor.ResetBone(BoneIndex);
	}

	ImGui::Separator();

	FTransform Transform;
	if (!Editor.GetSelectedBoneLocalTransform(Transform))
	{
		ImGui::TextDisabled("Select a bone in Bone Hierarchy.");
		return;
	}



	// 선택이 바뀌면 이전 본의 편집 상태가 새 본에 이어지지 않도록 ID를 구분합니다.
	ImGui::PushID(BoneIndex);
	bool bChanged = false;
	bChanged |= ImGui::DragFloat3("Location", Transform.Location.V, 0.1f);
	// 드래그 중 값이 튀지 않도록 마지막 오일러 값을 기억한다
	static FRotationConversionCache RotationCache;
	FRotator Euler = RotationCache.QuatToRotator(Transform.Rotation);
	if (ImGui::DragFloat3("Rotation", Euler.V, 0.5f))
	{
		Transform.Rotation = RotationCache.RotatorToQuat(Euler);
		bChanged = true;
	}
	bChanged |= ImGui::DragFloat3("Scale", Transform.Scale.V, 0.01f);
	ImGui::PopID();

	if (bChanged)
	{
		Editor.SetSelectedBoneLocalTransform(Transform);
	}
}
