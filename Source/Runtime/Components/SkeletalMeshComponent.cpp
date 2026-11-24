#include "EnginePCH.h"
#include "SkeletalMeshComponent.h"
#include "Serialization/MathSerialization.h"
#include "Animation/BonePose.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimGraphInstance.h"
#include "Animation/AnimBlueprintCompiler.h"
#include "Animation/AnimBlueprintFile.h"
#include "Asset/AssetUtils.h"
#include "Core/Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Casts.h"
#include "UObject/UObjectIterator.h"
#include "Render/LineBatcher.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/BlendSpaceFile.h"
#include "Asset/AssetManager.h"
#include <PxScene.h>
#include <PxSceneLock.h>
#include "Physics/BodyInstance.h"
#include "Physics/ConstraintInstance.h"
#include "Physics/PhysicsAsset.h"
#include "Physics/PhysicsScene.h"
#include "Engine/World.h"
#include <extensions/PxDefaultSimulationFilterShader.h>   // PxSetGroup, PxSetGroupCollisionFlag
#include <PxRigidDynamic.h>
#include "Physics/NvClothSDK.h"
#include "Physics/PhysXConversion.h"
#include <NvCloth/Cloth.h>
#include <NvCloth/Fabric.h>
#include <NvCloth/PhaseConfig.h>
#include <NvClothExt/ClothFabricCooker.h>
#include <limits>
#include "Physics/PhysicsSettings.h"
#include <NvCloth/Factory.h>
#include <NvCloth/Solver.h>

namespace
{
	// 래그돌 바디끼리는 충돌하지 않게 한 그룹으로 묶는다.
	// 씬이 PxDefaultSimulationFilterShader를 쓰므로 CollisionDisableTable 대신 그룹 충돌 표를 쓴다 (A와 합의 필요)
	constexpr physx::PxU16 RagdollCollisionGroup = 1;
}
void USkeletalMeshComponent::SetAnimInstance(UAnimInstance* InInstance)
{
	if (AnimInstance == InInstance)
	{
		return;
	}
	if (AnimInstance)
	{
		AnimInstance->UninitializeAnimation();
	}
	AnimInstance = InInstance;

	if (ManagedAnimGraph && ManagedAnimGraph.get() != InInstance)
	{
		ManagedAnimGraph.reset();
	}

	AnimBlueprintPath.clear();
	bBlueprintReloadRequested = false;

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();
	}
}

void USkeletalMeshComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!AnimBlueprintPath.empty() && (bBlueprintReloadRequested || !ManagedAnimGraph))
	{
		ReloadAnimBlueprint();
	}

	if (AnimInstance)
	{
		AnimInstance->InitializeAnimation();

		if (auto* Instance = GetSingleNodeInstance())
		{
			if (Instance->GetCurrentAsset())
			{
				Instance->SetPlaying(true);
			}
		}
	}
	RefreshClothSolver();
}

void USkeletalMeshComponent::TickComponent(float DeltaTime)
{
	if (bBlueprintReloadRequested)
	{
		bBlueprintReloadRequested = false;
		ReloadAnimBlueprint();
	}

	// 래그돌 중(bBlendPhysics)에는 애니 평가를 건너뛴다. 본은 BlendPhysicsBones가 채운다
	if (AnimInstance && GetSkeletalMesh() && !bBlendPhysics)
	{
		bUpdatingAnimation = true;
		if (auto* Instance = Cast<UAnimSingleNodeInstance>(AnimInstance))
		{
			Instance->SetBlendSpaceInput(BlendSpaceInput);
		}

		AnimInstance->UpdateAnimation(DeltaTime);
		FPoseContext Pose(AnimInstance, GetBones());
		AnimInstance->EvaluateAnimation(Pose);
		SetBoneSpaceTransforms(Pose.Pose);
		AnimInstance->TriggerAnimNotifies(DeltaTime);

		// 루트 모션: AnimInstance에 모인 delta를 월드 공간으로 바꿔 CharacterMovement가 가져갈 때까지 쌓아 둔다
		const FRootMotionMovementParams RootMotion = AnimInstance->ConsumeExtractedRootMotion();
		if (RootMotion.bHasRootMotion)
		{
			const FTransform WorldDelta = ConvertRootMotionToWorld(RootMotion.GetRootMotionTransform());
			PendingRootMotion.Location = (bHasPendingRootMotion ? PendingRootMotion.Location : FVector::ZeroVector) + WorldDelta.Location;
			PendingRootMotion.Rotation = bHasPendingRootMotion ? (WorldDelta.Rotation * PendingRootMotion.Rotation).Normalized() : WorldDelta.Rotation;
			bHasPendingRootMotion = true;
		}
		bUpdatingAnimation = false;
	}
	Super::TickComponent(DeltaTime);

	// 갱신된 본 위치를 kinematic 바디의 목표로. 이번 프레임 물리 스텝(PrePhysics 다음)이 따라간다
	UpdateKinematicBonesToAnim();
}

const TArray<FBoneInfo>& USkeletalMeshComponent::GetBones() const
{
	static const TArray<FBoneInfo> Empty{};
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetRefSkeleton() : Empty;
}

int32 USkeletalMeshComponent::FindBoneIndex(const FString& Name) const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->FindBoneIndex(Name) : -1;
}

FTransform USkeletalMeshComponent::GetBoneLocalTransform(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return FTransform();
	return BoneSpaceTransforms[BoneIndex];
}

void USkeletalMeshComponent::SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local)
{
	if (BoneIndex < 0 || BoneIndex >= BoneSpaceTransforms.Num()) return;
	BoneSpaceTransforms[BoneIndex] = Local;
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::SetBoneSpaceTransforms(const FCompactPose& Pose)
{
	if (Pose.GetNumBones() != GetBones().Num())
	{
		HTR_LOG(Warning, "SetBoneSpaceTransforms: pose has {} bones, mesh has {}.", Pose.GetNumBones(), GetBones().Num());
		return;
	}
#if defined(_DEBUG)
	for (int32 i = 0; i < Pose.GetNumBones(); ++i)
	{
		const FQuat& Q = Pose[i].Rotation;
		const float LenSq = Q.X * Q.X + Q.Y * Q.Y + Q.Z * Q.Z + Q.W * Q.W;
		if (std::abs(LenSq - 1.0f) > 1e-3f)
			HTR_LOG(Warning, "Pose bone {} rotation not normalized (|q|^2 = {})", i, LenSq);
	}
#endif
	BoneSpaceTransforms = Pose.GetBones();
	MarkBoneTransformsDirty();
}

FMatrix USkeletalMeshComponent::GetBoneComponentMatrix(int32 BoneIndex) const
{
	if (BoneIndex < 0 || BoneIndex >= ComponentSpaceTransforms.Num()) return FMatrix::Identity;
	return ComponentSpaceTransforms[BoneIndex];
}

FMatrix USkeletalMeshComponent::GetBoneWorldMatrix(int32 BoneIndex) const
{
	return GetBoneComponentMatrix(BoneIndex) * GetWorldMatrix();
}

void USkeletalMeshComponent::ResetPose()
{
	OnSkeletalMeshChanged();
	MarkBoneTransformsDirty();
}

void USkeletalMeshComponent::RefreshBoneTransformsNow()
{
	UpdateBoneTransformsNow();
}

void USkeletalMeshComponent::RefreshBoneTransforms()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num())
		OnSkeletalMeshChanged();

	ComponentSpaceTransforms.SetNum(Bones.Num());
	for (int i = 0; i < Bones.Num(); ++i)
	{
		const FMatrix Local = BoneSpaceTransforms[i].GetLocalMatrix();
		const int32 Parent = Bones[i].ParentIndex;
		ComponentSpaceTransforms[i] = Parent < 0 ? Local : Local * ComponentSpaceTransforms[Parent];
	}
}

void USkeletalMeshComponent::Serialize(FStructuredArchive::FRecord Record)
{
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimInstance* SavedInstance = AnimInstance;

	if (Ar.IsSaving() && (!AnimBlueprintPath.empty() || ManagedAnimGraph))
	{
		AnimInstance = nullptr;
	}

	Super::Serialize(Record);

	if (Ar.IsSaving())
	{
		AnimInstance = SavedInstance;
	}

	int32 Num = Ar.IsSaving() ? BoneSpaceTransforms.Num() : 0;
	FStructuredArchive::FArray PoseArray = Record.EnterArray("BonePose", Num);

	for (int32 I = 0; I < Num; ++I)
	{
		FStructuredArchive::FRecord BoneRecord = PoseArray.EnterElement().EnterRecord();
		FString Name = Ar.IsSaving() ? GetBones()[I].Name : FString();
		FTransform Local = Ar.IsSaving() ? BoneSpaceTransforms[I] : FTransform();
		BoneRecord << SA_VALUE("Name", Name) << SA_VALUE("Local", Local);

		if (Ar.IsLoading())
		{
			// 메시의 본 순서가 바뀌어도 맞도록 이름으로 찾는다
			const int32 Index = FindBoneIndex(Name);
			if (Index >= 0)
				BoneSpaceTransforms[Index] = Local;
		}
	}

	SerializeSingleAnimation(Record);

	if (Ar.IsLoading())
	{
		if (!AnimBlueprintPath.empty())
		{
			ReloadAnimBlueprint();
		}

		RefreshBoneTransformsNow();
	}
}

void USkeletalMeshComponent::InitArticulated(FPhysicsScene* PhysScene)
{
	UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (!PhysAsset || !PhysScene || Bodies.Num() > 0) return;

	RefreshBoneTransformsNow();
	InstantiatePhysicsAssetBodies_Internal(*PhysAsset, *PhysScene);

	// UE InitArticulated: 루트 바디를 다시 찾는다
	RootBodyData.BodyIndex = INDEX_NONE;
	SetRootBodyIndex(FindRootBodyIndex());

	for (UPhysicsConstraintTemplate* Template : PhysAsset->ConstraintSetup)
	{
		const FConstraintInstance& Src = Template->DefaultInstance;
		FBodyInstance* Body1 = GetBodyInstance(Src.ConstraintBone1);
		FBodyInstance* Body2 = GetBodyInstance(Src.ConstraintBone2);
		if (!Body1 || !Body2) continue;

		FConstraintInstance* CI = new FConstraintInstance(Src);
		// 조인트 위치(Pos1/Pos2)는 본 로컬 공간 값이라 바디와 같은 스케일(본 월드 스케일)을 곱해야 한다.
		// UE는 본 스케일이 1이라 컴포넌트 스케일을 쓰지만, UAL은 Armature 본에 ×100이 있어 둘이 다르다
		const float Scale = FTransform::FromMatrix(GetBoneWorldMatrix(Body1->InstanceBoneIndex)).Scale.X;
		if (!CI->InitConstraint(Body1, Body2, Scale)) { delete CI; continue; }
		Constraints.Add(CI);
	}

	RegisterEndPhysicsTick(true);

	if (bSimulatePhysics) SetSimulatePhysics(true);
}

void USkeletalMeshComponent::TermArticulated()
{
	RegisterEndPhysicsTick(false);
	for (FConstraintInstance* CI : Constraints) { CI->TermConstraint(); delete CI; }
	Constraints.Empty();
	for (FBodyInstance* BI : Bodies)
	{
		if (BI)
		{
			BI->TermBody();
			delete BI;
		}
	}
	Bodies.Empty();
	ResetRootBodyIndex();
	bBlendPhysics = false;
}

void USkeletalMeshComponent::SetSimulatePhysics(bool bEnabled)
{
	const bool bWasSimulating = bBlendPhysics;
	bSimulatePhysics = bEnabled;
	bBlendPhysics = bEnabled;

	// [Hitori 추가] 래그돌을 끄면 루트 바디가 수평으로 움직인 만큼만 옮기고 켜기 전 자세(회전·높이)로 세운다.
	// UE는 컴포넌트를 쓰러진 방향 그대로 두고 일어나기는 게임 코드(Get-up 애니메이션 등)에 맡긴다
	FBodyInstance* RootBody = GetBodyInstance();
	if (bEnabled && !bWasSimulating && RootBody)
	{
		PreRagdollComponentTransform = FTransform::FromMatrix(GetWorldMatrix());
		PreRagdollRootBodyLocation = RootBody->GetWorldTransform().Location;
	}
	else if (!bEnabled && bWasSimulating && RootBody)
	{
		const FVector Moved = RootBody->GetWorldTransform().Location - PreRagdollRootBodyLocation;
		FTransform Upright = PreRagdollComponentTransform;
		Upright.Location.X += Moved.X;
		Upright.Location.Y += Moved.Y;
		SetWorldTransform(Upright.GetLocalMatrix(), false);
	}

	if (UPhysicsAsset* PhysAsset = GetPhysicsAsset())
	{
		for (int32 i = 0; i < Bodies.Num(); ++i)
		{
			if (Bodies[i] && PhysAsset->SkeletalBodySetups.IsValidIndex(i) && PhysAsset->SkeletalBodySetups[i]->PhysicsType == PhysType_Default)
				Bodies[i]->SetInstanceSimulatePhysics(bEnabled);
		}
	}

	// 켜는 순간의 컴포넌트-루트 바디 관계를 저장한다. 이후 컴포넌트는 이 관계를 유지하며 루트 바디를 따라간다
	if (bEnabled)
		SetRootBodyIndex(RootBodyData.BodyIndex);
}

void USkeletalMeshComponent::SetAllBodiesSimulatePhysics(bool bNewSimulate)
{
	for (FBodyInstance* BI : Bodies)
	{
		if (BI) BI->SetInstanceSimulatePhysics(bNewSimulate);
	}
}

FBodyInstance* USkeletalMeshComponent::GetBodyInstance(FName BoneName) const
{
	if (BoneName == NAME_None)
		return Bodies.IsValidIndex(RootBodyData.BodyIndex) ? Bodies[RootBodyData.BodyIndex] : nullptr;

	const UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	const int32 Index = PhysAsset ? PhysAsset->FindBodyIndex(BoneName) : INDEX_NONE;
	return Bodies.IsValidIndex(Index) ? Bodies[Index] : nullptr;
}

UPhysicsAsset* USkeletalMeshComponent::GetPhysicsAsset() const
{
	return GetSkeletalMesh() ? GetSkeletalMesh()->GetPhysicsAsset() : nullptr;
}

// UE FindRootBodyIndex: 본 순서(부모가 앞)대로 돌며 처음 만나는 바디
int32 USkeletalMeshComponent::FindRootBodyIndex() const
{
	int32 RootBodyIndex = RootBodyData.BodyIndex;
	const UPhysicsAsset* PhysAsset = GetPhysicsAsset();
	if (RootBodyIndex == INDEX_NONE && PhysAsset)
	{
		for (const FBoneInfo& Bone : GetBones())
		{
			const int32 BodyIndex = PhysAsset->FindBodyIndex(FName(Bone.Name));
			if (BodyIndex != INDEX_NONE)
			{
				RootBodyIndex = BodyIndex;
				break;
			}
		}
	}
	return RootBodyIndex;
}

// UE SetRootBodyIndex: TransformToRoot = 루트 바디 기준 컴포넌트 Transform (UE GetRelativeTransform)
void USkeletalMeshComponent::SetRootBodyIndex(int32 InBodyIndex)
{
	if (InBodyIndex == INDEX_NONE) return;

	RootBodyData.BodyIndex = InBodyIndex;
	RootBodyData.TransformToRoot = FTransform();
	if (Bodies.Num() == 0) return;

	if (Bodies.IsValidIndex(InBodyIndex) && Bodies[InBodyIndex])
	{
		// 행 벡터 행렬이라 Comp = ToRoot * Body, 즉 ToRoot = Comp * Body^-1. 컴포넌트 스케일(0.01)은 ToRoot에 남는다
		const FMatrix BodyWorld = Bodies[InBodyIndex]->GetWorldTransform().GetLocalMatrix();
		RootBodyData.TransformToRoot = FTransform::FromMatrix(GetWorldMatrix() * BodyWorld.Inverse());
	}
	else
	{
		ResetRootBodyIndex();
	}
}

void USkeletalMeshComponent::ResetRootBodyIndex()
{
	RootBodyData.BodyIndex = INDEX_NONE;
	RootBodyData.TransformToRoot = FTransform();
}

// UE GetComponentTransformFromBodyInstance (SimulationUpatesComponentTransform 모드)
FTransform USkeletalMeshComponent::GetComponentTransformFromBodyInstance(FBodyInstance* UseBI)
{
	return FTransform::FromMatrix(RootBodyData.TransformToRoot.GetLocalMatrix() * UseBI->GetWorldTransform().GetLocalMatrix());
}

void USkeletalMeshComponent::SerializeSingleAnimation(FStructuredArchive::FRecord& Record)
{
	// 디테일 패널에서 고른 단일 애니메이션(SingleNodeInstance의 에셋)은 AnimInstance 객체가 아니라
	// 에셋 경로로 저장한다. 없으면 필드를 쓰지 않으므로 예전 씬도 그대로 읽힌다.
	FArchive& Ar = Record.GetUnderlyingArchive();
	UAnimSingleNodeInstance* Instance = GetSingleNodeInstance();
	UAnimationAsset* Asset = Instance ? Instance->GetCurrentAsset() : nullptr;
	const bool bHasAnimation = Ar.IsSaving() && Asset && !Asset->GetAssetPath().empty();

	TOptional<FStructuredArchive::FSlot> AnimSlot = Record.TryEnterField("SingleAnimation", bHasAnimation);
	if (!AnimSlot.IsSet())
		return;

	FStructuredArchive::FRecord AnimRecord = AnimSlot.GetValue().EnterRecord();
	FString Path = Ar.IsSaving() ? Asset->GetAssetPath() : FString();
	bool bLoop = Ar.IsSaving() ? Instance->IsLooping() : true;
	float Rate = Ar.IsSaving() ? Instance->GetPlayRate() : 1.0f;
	AnimRecord << SA_VALUE("Asset", Path) << SA_VALUE("Looping", bLoop) << SA_VALUE("PlayRate", Rate);

	if (!Ar.IsLoading())
		return;

	// AssetManager 캐시로 불러온다 (디테일 패널이 고를 때와 같은 객체)
	UAnimationAsset* Loaded = BlendSpaceFile::IsBlendSpacePath(Path)
		? static_cast<UAnimationAsset*>(UAssetManager::LoadBlendSpace(Path))
		: static_cast<UAnimationAsset*>(UAssetManager::LoadAnimSequence(Path));
	if (!Loaded)
	{
		HTR_LOG(Warning, "SkeletalMeshComponent: single animation not found: {}", Path);
		return;
	}

	// 재생 시작은 BeginPlay가 한다 (에디터 월드에서는 첫 프레임 포즈만 보인다)
	SetAnimation(Loaded);
	if (UAnimSingleNodeInstance* NewInstance = GetSingleNodeInstance())
	{
		NewInstance->SetLooping(bLoop);
		NewInstance->SetPlayRate(Rate);
	}
}
void USkeletalMeshComponent::OnRegister()
{
	Super::OnRegister();
	RefreshClothSolver();
}
void USkeletalMeshComponent::OnUnregister()
{
	ReleaseClothSolver();
	Super::OnUnregister();
}
void USkeletalMeshComponent::OnSkeletalMeshChanged()
{
	bBlueprintReloadRequested = !AnimBlueprintPath.empty();
	BoneSpaceTransforms.Reset();
	for (const FBoneInfo& Bone : GetBones())
		BoneSpaceTransforms.Add(Bone.LocalBindPose);
	RefreshClothSolver();
}
void USkeletalMeshComponent::RefreshClothSolver()
{
	UWorld* World = GetWorld();
	const USkeletalMesh* Mesh = GetSkeletalMesh();
	if (!IsRegistered() || !HasBegunPlay() || !World || !World->IsGameWorld() || !Mesh)
	{
		ReleaseClothSolver();
		return;
	}
	if (ClothSourceMesh == Mesh){ return; }

	ReleaseClothSolver();
	const auto& Bindings = Mesh->GetClothSectionBindings();
	nv::cloth::Factory* Factory = NvClothSDK::GetFactory();
	if (Bindings.IsEmpty() || !Factory){ return; }

	// 바인딩별 Cloth 생성과 초기 월드 중력 적용
	const FSkeletalMeshLODRenderData& LOD = Mesh->GetRenderData().LODRenderData[0];
	const float GravityZ = World->IsWorldGravityOverridden()
		? World->GetWorldGravityZ() : FPhysicsSettings::Get().DefaultGravityZ;
	TArray<TUniquePtr<nv::cloth::Cloth>> NewCloths;
	NewCloths.Reserve(Bindings.Num());

	for (const FClothSectionBinding& Binding : Bindings)
	{
		TUniquePtr<nv::cloth::Cloth> Cloth = CreateClothInstance(*Factory, LOD, Binding, GravityZ);
		if (!Cloth)
		{
			HTR_LOG(Warning, "Failed to create cloth for section {}", Binding.SectionIndex);
			return;
		}
		NewCloths.Add(std::move(Cloth));
	}

	TUniquePtr<nv::cloth::Solver> NewSolver(Factory->createSolver());
	if (!NewSolver)
	{
		HTR_LOG(Warning, "Failed to create component cloth solver");
		return;
	}

	// 준비된 Cloth의 등록과 틱 실행 연결
	for (const auto& Cloth : NewCloths)
	{
		NewSolver->addCloth(Cloth.get());
	}

	ClothInstances = std::move(NewCloths);
	ClothSolver = std::move(NewSolver);
	ClothSourceMesh = Mesh;
	ClothTickFunction.RegisterTickFunction(World->GetTickTaskManager());
}
// 바인딩과 렌더 섹션으로부터 Cloth 인스턴스 생성
TUniquePtr<nv::cloth::Cloth> USkeletalMeshComponent::CreateClothInstance(
	nv::cloth::Factory& Factory, const FSkeletalMeshLODRenderData& LOD,
	const FClothSectionBinding& Binding, float GravityZ) const
{
	const FSkeletalMeshSection& Section = LOD.Sections[Binding.SectionIndex];
	const FVector Scale = GetWorldScale3D();
	if (Scale.X <= 0.0f || Scale.Y <= 0.0f || Scale.Z <= 0.0f)
	{
		return {};
	}

	// 전역 렌더 정점에서 연속 Cloth 입자로의 변환
	TArray<int32> RenderToParticle;
	RenderToParticle.Init(INDEX_NONE, LOD.BindVertices.Num());
	TArray<physx::PxVec3> Positions;
	TArray<uint32> Indices;
	Indices.Reserve(Section.IndexCount);
	const uint32 End = Section.StartIndex + Section.IndexCount;

	for (uint32 Index = Section.StartIndex; Index < End; ++Index)
	{
		const uint32 RenderVertex = LOD.Indices[Index];
		int32& Particle = RenderToParticle[RenderVertex];
		if (Particle == INDEX_NONE)
		{
			Particle = Positions.Num();
			Positions.Add(U2PVector(LOD.BindVertices[RenderVertex].Position * Scale));
		}
		Indices.Add(static_cast<uint32>(Particle));
	}

	// Windows CPU Fabric의 16비트 인덱스와 SIMD 패딩 상한
	constexpr uint32 MaxCpuParticles = (std::numeric_limits<uint16>::max)() - 7;
	const uint32 ParticleCount = static_cast<uint32>(Positions.Num());
	if (ParticleCount > MaxCpuParticles)
	{
		return {};
	}

	// 바인딩에서 지정한 고정점의 역질량 설정
	TArray<float> InverseMasses;
	InverseMasses.Init(1.0f, ParticleCount);
	uint32 MovableParticleCount = ParticleCount;
	for (const uint32 RenderVertex : Binding.PinnedRenderVertices)
	{
		float& InverseMass = InverseMasses[RenderToParticle[RenderVertex]];
		if (InverseMass > 0.0f)
		{
			InverseMass = 0.0f;
			--MovableParticleCount;
		}
	}
	// 이동 가능한 입자가 없는 섹션의 쿠킹 생략
	if (MovableParticleCount == 0)
	{
		return {};
	}

	// 추출한 위치, 삼각형, 역질량의 Fabric 쿠킹
	nv::cloth::ClothMeshDesc Desc;
	Desc.points.data = Positions.GetData();
	Desc.points.count = ParticleCount;
	Desc.points.stride = sizeof(physx::PxVec3);
	Desc.triangles.data = Indices.GetData();
	Desc.triangles.count = Indices.Num() / 3;
	Desc.triangles.stride = sizeof(uint32) * 3;
	Desc.invMasses.data = InverseMasses.GetData();
	Desc.invMasses.count = ParticleCount;
	Desc.invMasses.stride = sizeof(float);

	const physx::PxQuat Rotation = U2PQuat(GetWorldRotation().Quaternion());
	const physx::PxVec3 CookDown = Rotation.rotateInv(physx::PxVec3(0.0f, 0.0f, -1.0f));
	nv::cloth::Fabric* Fabric = NvClothCookFabricFromMesh(&Factory, Desc, CookDown, nullptr, false);
	if (!Fabric)
	{
		return {};
	}

	// 초기 입자 생성과 쿠킹 참조 반환
	TArray<physx::PxVec4> Particles;
	Particles.Reserve(ParticleCount);
	for (uint32 Particle = 0; Particle < ParticleCount; ++Particle)
	{
		Particles.Emplace(Positions[Particle], InverseMasses[Particle]);
	}

	const uint32 PhaseCount = Fabric->getNumPhases();
	TUniquePtr<nv::cloth::Cloth> Cloth(Factory.createCloth(
		{ Particles.GetData(), Particles.GetData() + ParticleCount }, *Fabric));
	Fabric->decRefCount();
	if (!Cloth)
	{
		return {};
	}

	// 기본 제약 그룹과 초기 월드 자세의 적용
	TArray<nv::cloth::PhaseConfig> Phases;
	Phases.Reserve(PhaseCount);
	for (uint32 Phase = 0; Phase < PhaseCount; ++Phase)
	{
		Phases.Emplace(static_cast<uint16>(Phase));
	}
	Cloth->setPhaseConfig({ Phases.GetData(), Phases.GetData() + Phases.Num() });
	Cloth->teleportToLocation(U2PVector(GetWorldLocation()), Rotation);
	Cloth->setGravity(physx::PxVec3(0.0f, 0.0f, GravityZ));
	return Cloth;
}
void USkeletalMeshComponent::ReleaseClothSolver()
{
	ClothTickFunction.UnRegisterTickFunction();
	for (const auto& Cloth : ClothInstances)
	{
		ClothSolver->removeCloth(Cloth.get());
	}

	ClothInstances.Empty();
	ClothSolver.reset();
	ClothSourceMesh = nullptr;

}
void USkeletalMeshComponent::TickClothing()
{
	const UWorld* World = GetWorld();
	const FPhysicsScene* PhysicsScene = World ? World->GetPhysicsScene() : nullptr;
	if (!ClothSolver || !HasBegunPlay() || !PhysicsScene)
	{
		return;
	}

	// 해당 프레임의 완료 횟수와 공용 고정 간격
	const int32 StepCount = PhysicsScene->GetLastTickStepCount();
	const float StepDelta = static_cast<float>(FPhysicsSettings::Get().FixedStepSeconds);

	for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
	{
		// 계산 대상이 있는 스텝의 시작
		if (!ClothSolver->beginSimulation(StepDelta))
		{
			return;
		}

		// 현재 스텝의 전체 청크 순차 계산
		const int32 ChunkCount = ClothSolver->getSimulationChunkCount();
		for (int32 ChunkIndex = 0; ChunkIndex < ChunkCount; ++ChunkIndex)
		{
			ClothSolver->simulateChunk(ChunkIndex);
		}

		// 모든 청크 계산 이후의 스텝 완료
		ClothSolver->endSimulation();
	}
}
void USkeletalMeshComponent::OnCreatePhysicsState()
{
	Super::OnCreatePhysicsState();
	InitArticulated(GetWorld()->GetPhysicsScene());
}
void USkeletalMeshComponent::OnDestroyPhysicsState()
{
	TermArticulated();
	Super::OnDestroyPhysicsState();
}
void USkeletalMeshComponent::InstantiatePhysicsAssetBodies_Internal(const UPhysicsAsset& PhysAsset, FPhysicsScene& PhysScene)
{
	physx::PxSetGroupCollisionFlag(RagdollCollisionGroup, RagdollCollisionGroup, false);

	Bodies.Init(nullptr, PhysAsset.SkeletalBodySetups.Num());
	for (int32 i = 0; i < PhysAsset.SkeletalBodySetups.Num(); ++i)
	{
		const USkeletalBodySetup* Setup = PhysAsset.SkeletalBodySetups[i];
		const int32 Bone = FindBoneIndex(Setup->BoneName.ToString());
		if (Bone == INDEX_NONE) continue;

		const EPhysicsBodyType Type = Setup->PhysicsType == PhysType_Simulated ? EPhysicsBodyType::Dynamic : EPhysicsBodyType::Kinematic;

		FBodyInstance* BI = new FBodyInstance();
		BI->InstanceBodyIndex = i;
		BI->InstanceBoneIndex = Bone;
		if (!BI->InitBody(*Setup, FTransform::FromMatrix(GetBoneWorldMatrix(Bone)), this, PhysScene, Type))
		{
			HTR_LOG(Warning, "Ragdoll body failed: {}", Setup->BoneName.ToString());
			delete BI;
			continue;
		}
		if (physx::PxRigidDynamic* Dyn = BI->GetRigidDynamic())
		{
			// 등록된 래그돌 Shape 필터 갱신의 쓰기 잠금
			const physx::PxSceneWriteLock WriteLock(*PhysScene.GetPxScene());
			physx::PxSetGroup(*Dyn, RagdollCollisionGroup);
		}
		Bodies[i] = BI;
	}
}
void USkeletalMeshComponent::UpdateKinematicBonesToAnim()
{
	if (bBlendPhysics) return;
	for (FBodyInstance* BI : Bodies)
	{
		if (BI && BI->IsValidBodyInstance())
		{
			BI->SetKinematicTarget(FTransform::FromMatrix(GetBoneWorldMatrix(BI->InstanceBoneIndex)));
		}
	}
}
void USkeletalMeshComponent::BlendPhysicsBones()
{
	const TArray<FBoneInfo>& Bones = GetBones();
	if (BoneSpaceTransforms.Num() != Bones.Num()) return;

	const FMatrix CompWorld = GetWorldMatrix();
	const FMatrix InvComp = CompWorld.Inverse();
	const FVector CompScale = FTransform::FromMatrix(CompWorld).Scale;

	TArray<FMatrix> Comp;
	Comp.SetNum(Bones.Num());
	TArray<uint8> bFromPhys;
	bFromPhys.Init(0, Bones.Num());

	for (FBodyInstance* BI : Bodies)
	{
		if (!BI || !BI->IsValidBodyInstance()) continue;
		FTransform BodyWorld = BI->GetWorldTransform();
		// PhysX 포즈에는 스케일이 없다. 바인드 포즈의 컴포넌트 공간 스케일(UAL Armature ×100)에 컴포넌트 스케일을 곱해 붙인다.
		// 현재 본 스케일을 다시 읽으면 매 프레임 분해 오차가 쌓여 메시가 계속 늘어난다
		const FVector RefScale = FTransform::FromMatrix(Bones[BI->InstanceBoneIndex].InverseBindPose.Inverse()).Scale;
		BodyWorld.Scale = FVector(RefScale.X * CompScale.X, RefScale.Y * CompScale.Y, RefScale.Z * CompScale.Z);
		Comp[BI->InstanceBoneIndex] = BodyWorld.GetLocalMatrix() * InvComp;
		bFromPhys[BI->InstanceBoneIndex] = 1;
	}

	for (int32 i = 0; i < Bones.Num(); ++i)
	{
		const int32 P = Bones[i].ParentIndex;
		if (!bFromPhys[i])
		{
			Comp[i] = P < 0 ? BoneSpaceTransforms[i].GetLocalMatrix() : BoneSpaceTransforms[i].GetLocalMatrix() * Comp[P];
		}
		else if (P >= 0)
		{
			BoneSpaceTransforms[i] = FTransform::FromMatrix(Comp[i] * Comp[P].Inverse());
		}
		else
		{
			BoneSpaceTransforms[i] = FTransform::FromMatrix(Comp[i]);
		}
	}
	MarkBoneTransformsDirty();
}
void USkeletalMeshComponent::RegisterEndPhysicsTick(bool bRegister)
{
	if (!bRegister)
	{
		EndPhysicsTickFunction.UnRegisterTickFunction();
		return;
	}
	if (UWorld* World = GetWorld(); World && !EndPhysicsTickFunction.IsTickFunctionRegistered())
		EndPhysicsTickFunction.RegisterTickFunction(World->GetTickTaskManager());
}

void USkeletalMeshComponent::EndPhysicsTickComponent(float DeltaTime)
{
	// UE EndPhysicsTickComponent: 먼저 컴포넌트를 루트 바디로 옮기고, 옮겨진 컴포넌트 기준으로 본을 맞춘다
	SyncComponentToRBPhysics();   // bSimulatePhysics가 아니면 안에서 바로 return
	if (!bBlendPhysics) return;
	BlendPhysicsBones();
	RefreshBoneTransformsNow();  // 같은 프레임에 스키닝까지 (UE도 여기서 본 변환을 확정한다)
}

UAnimSingleNodeInstance* USkeletalMeshComponent::GetSingleNodeInstance() const
{
	return Cast<UAnimSingleNodeInstance>(AnimInstance);
}

void USkeletalMeshComponent::SetAnimation(UAnimationAsset* NewAnimToPlay)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		HTR_LOG(Warning, "SetAnimation: unsupported animation asset type");
		return;
	}
	AnimBlueprintPath.clear();
	AnimBlueprintError.clear();
	bBlueprintReloadRequested = false;
	auto* SingleNodeInstance = Cast<UAnimSingleNodeInstance>(AnimInstance);

	if (!SingleNodeInstance)
	{
		SingleNodeInstance = NewObject<UAnimSingleNodeInstance>(this);
		SetAnimInstance(SingleNodeInstance);
	}

	SingleNodeInstance->SetAnimationAsset(NewAnimToPlay);
	SingleNodeInstance->SetBlendSpaceInput(BlendSpaceInput);

	RefreshAnimationPose();
}

void USkeletalMeshComponent::Play(bool bLooping)
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetLooping(bLooping);
		Instance->SetPlaying(true);
	}
}

void USkeletalMeshComponent::Stop()
{
	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetPlaying(false);
	}
}

bool USkeletalMeshComponent::IsPlaying() const
{
	const auto* Instance = GetSingleNodeInstance();
	return Instance && Instance->IsPlaying();
}

void USkeletalMeshComponent::PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLooping)
{
	if (NewAnimToPlay && !Cast<UAnimSequence>(NewAnimToPlay) && !Cast<UBlendSpace1D>(NewAnimToPlay))
	{
		return;
	}

	SetAnimation(NewAnimToPlay);
	Play(bLooping);
}

void USkeletalMeshComponent::RefreshAnimationPose()
{
	if (!AnimInstance)
	{
		ResetPose();
		RefreshBoneTransformsNow();
		return;
	}

	if (auto* Instance = GetSingleNodeInstance())
	{
		Instance->SetBlendSpaceInput(BlendSpaceInput);
	}

	FPoseContext Pose(AnimInstance, GetBones());
	AnimInstance->EvaluateAnimation(Pose);
	SetBoneSpaceTransforms(Pose.Pose);
	RefreshBoneTransformsNow();
}


FTransform USkeletalMeshComponent::ConsumeRootMotion()
{
	const FTransform Result = PendingRootMotion;
	PendingRootMotion = FTransform();
	bHasPendingRootMotion = false;
	return Result;
}

FTransform USkeletalMeshComponent::ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const
{
	// delta는 root 본의 부모 공간 값이다. UAL은 Armature → root 구조라 부모(Armature)를 거쳐야 컴포넌트 공간이 된다.
	const TArray<FBoneInfo>& Bones = GetBones();
	const int32 RootIndex = GetRootMotionBoneIndex();
	const int32 ParentIndex = RootIndex >= 0 ? Bones[RootIndex].ParentIndex : -1;

	// 이동: 부모(바인드 포즈) → 컴포넌트 → 월드 행렬로 방향·스케일만 적용 (Armature 스케일, 메시 0.01, yaw −180 모두 여기서)
	const FMatrix ComponentToWorld = GetWorldMatrix();
	const FMatrix ParentToWorld = ParentIndex >= 0
		? Bones[ParentIndex].InverseBindPose.Inverse() * ComponentToWorld
		: ComponentToWorld;

	FTransform Result;
	Result.Location = ParentToWorld.TransformVector(ParentSpaceDelta.Location);

	// 회전: 부모 공간 회전을 월드 공간 회전으로 바꾼다 (P × Δ × P⁻¹, P = 컴포넌트 월드 회전 × 부모 본 회전)
	FQuat ParentRotation = FQuat(GetWorldRotation());
	FQuat BoneChainRotation;
	for (int32 Index = ParentIndex; Index >= 0; Index = Bones[Index].ParentIndex)
	{
		BoneChainRotation = Bones[Index].LocalBindPose.Rotation * BoneChainRotation;
	}
	ParentRotation = (ParentRotation * BoneChainRotation).Normalized();
	Result.Rotation = (ParentRotation * ParentSpaceDelta.Rotation * ParentRotation.Inverse()).Normalized();
	return Result;
}

int32 USkeletalMeshComponent::GetRootMotionBoneIndex() const
{
	const int32 RootIndex = FindBoneIndex("root");
	if (RootIndex >= 0)
	{
		return RootIndex;
	}
	return GetBones().IsEmpty() ? -1 : 0;
}

void USkeletalMeshComponent::DrawRootDebug(FLineBatcher* LineBatcher) const
{
	const int32 RootIndex = GetRootMotionBoneIndex();
	if (!LineBatcher || RootIndex < 0 || RootIndex >= ComponentSpaceTransforms.Num())
	{
		return;
	}

	// root 본 위치에 구체
	const FVector Root = GetBoneWorldMatrix(RootIndex).TransformPosition(FVector::ZeroVector);
	const FVector4 Color(1.0f, 0.3f, 0.8f, 1.0f);
	LineBatcher->AddSphere(Root, 0.08f, Color);

	// 액터 위치를 root 높이로 내린 점 → root. 선 길이 = root가 액터(캡슐)에서 벗어난 거리
	if (const AActor* Owner = GetOwner())
	{
		const FVector ActorLocation = Owner->GetActorLocation();
		const FVector Base(ActorLocation.X, ActorLocation.Y, Root.Z);
		LineBatcher->AddLine(Base, Root, Color);
		LineBatcher->AddSphere(Base, 0.03f, Color);
	}
}
bool GShowSkeletalRootDebug = false;

void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher)
{
	if (!World || !LineBatcher) return;
	for (TObjectIterator<USkeletalMeshComponent> It; It; ++It)
	{
		const USkeletalMeshComponent* Mesh = *It;
		if (!Mesh || Mesh->GetWorld() != World || !Mesh->IsRegistered()) continue;
		Mesh->DrawRootDebug(LineBatcher);
	}
}

USkeletalMeshComponent::USkeletalMeshComponent()
{
	EndPhysicsTickFunction.Target = this;
	EndPhysicsTickFunction.bCanEverTick = true;
	EndPhysicsTickFunction.TickGroup = ETickingGroup::PostPhysics;
	ClothTickFunction.Target = this;
	ClothTickFunction.bCanEverTick = true;
	ClothTickFunction.TickGroup = ETickingGroup::PostUpdateWork;
}

USkeletalMeshComponent::~USkeletalMeshComponent()
{
	ReleaseClothSolver();
	if (ManagedAnimGraph)
	{
		if (AnimInstance == ManagedAnimGraph.get())
		{
			SetAnimInstance(nullptr);
		}
		else
		{
			ManagedAnimGraph->UninitializeAnimation();
		}
	}
}

const FString& USkeletalMeshComponent::GetAnimBlueprintPath() const
{
	return AnimBlueprintPath;
}

const FString& USkeletalMeshComponent::GetAnimBlueprintError() const
{
	return AnimBlueprintError;
}

bool USkeletalMeshComponent::SetAnimBlueprint(const FString& Path)
{
	if (bUpdatingAnimation)
	{
		AnimBlueprintError = "Load or reload Animation Blueprints from actor Tick, outside animation callbacks.";
		return false;
	}

	if (Path.empty())
	{
		const bool bHadBlueprint = !AnimBlueprintPath.empty();
		AnimBlueprintPath.clear();
		AnimBlueprintError.clear();
		bBlueprintReloadRequested = false;

		if (ManagedAnimGraph && bHadBlueprint)
		{
			SetAnimInstance(nullptr);
			ResetPose();
			RefreshBoneTransformsNow();
		}

		return true;
	}

	AnimBlueprintError.clear();
	const FString Key = FAssetUtils::GetAssetKey(Path);
	const FString FilePath = FAssetUtils::ResolveAssetFilePath(Key);
	FAnimBlueprintData Data;

	if (!AnimBlueprintFile::IsAnimBlueprintPath(Key) || !AnimBlueprintFile::Read(FPaths::FromUtf8(FilePath.empty() ? Key : FilePath), Data, AnimBlueprintError))
	{
		if (AnimBlueprintError.empty())
		{
			AnimBlueprintError = "Use an .animblueprint asset.";
		}

		return false;
	}

	TUniquePtr<UAnimGraphInstance> Instance;
	AnimBlueprintCompiler::FNodeMap NodeIndices;

	if (!AnimBlueprintCompiler::Build(Data, *this, Instance, NodeIndices, AnimBlueprintError))
	{
		return false;
	}

	SetAnimInstance(Instance.get());
	ManagedAnimGraph = std::move(Instance);
	AnimBlueprintPath = Key;
	bBlueprintReloadRequested = false;
	AnimBlueprintError.clear();
	RefreshAnimationPose();
	return true;
}

bool USkeletalMeshComponent::ReloadAnimBlueprint()
{
	const FString Path = AnimBlueprintPath;
	bBlueprintReloadRequested = false;

	if (Path.empty())
	{
		return false;
	}

	if (!SetAnimBlueprint(Path))
	{
		HTR_LOG(Warning, "AnimBlueprint load failed: {} ({})", Path, AnimBlueprintError);
		return false;
	}

	return true;
}

UAnimGraphInstance* USkeletalMeshComponent::CreateAnimGraph()
{
	if (bUpdatingAnimation)
	{
		AnimBlueprintError = "Create animation graphs outside animation callbacks.";
		return nullptr;
	}

	if (!AnimBlueprintPath.empty())
	{
		AnimBlueprintError = "An Animation Blueprint is assigned. Use GetAnimGraph to update its variables.";
		return nullptr;
	}

	AnimBlueprintError.clear();

	if (UAnimGraphInstance* Existing = Cast<UAnimGraphInstance>(AnimInstance))
	{
		Existing->ResetGraph();
		return Existing;
	}

	TUniquePtr<UAnimGraphInstance> Instance(NewObject<UAnimGraphInstance>(this));
	SetAnimInstance(Instance.get());
	ManagedAnimGraph = std::move(Instance);
	return ManagedAnimGraph.get();
}

void FSkeletalMeshComponentEndPhysicsTickFunction::ExecuteTick(float DeltaTime)
{
	if (Target) Target->EndPhysicsTickComponent(DeltaTime);
}
void FSkeletalMeshComponentClothTickFunction::ExecuteTick(float /*DeltaTime*/)
{
	if (Target)
	{
		Target->TickClothing();
	}
}