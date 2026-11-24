#pragma once

#include "SkinnedMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "SkeletalMeshComponent.generated.h"

class UAnimInstance;
class UAnimSequence;
class UBlendSpace1D;
class UWorld;
class FLineBatcher;

// World의 SkeletalMeshComponent마다 root 디버그를 그린다 (DrawCollisionShapes와 같은 방식). PIE의 Show Root에서 호출
void DrawSkeletalRootDebug(UWorld* World, FLineBatcher* LineBatcher);
// Settings > Debug Draw (PIE) > Show Root 값. 에디터가 매 프레임 갱신하고, 런타임 액터("root" 라벨 등)가 읽는다
extern bool GShowSkeletalRootDebug;
class UAnimationAsset;
class UAnimSingleNodeInstance;
class UAnimGraphInstance;

struct FBodyInstance;
struct FConstraintInstance;
class FPhysicsScene;
class UPhysicsAsset;
class USkeletalMeshComponent;

struct FSkeletalMeshComponentEndPhysicsTickFunction : public FTickFunction
{
	USkeletalMeshComponent* Target = nullptr;
	void ExecuteTick(float DeltaTime) override;
};

UCLASS()
class USkeletalMeshComponent : public USkinnedMeshComponent
{
	GENERATED_BODY()

public:
    USkeletalMeshComponent();
    ~USkeletalMeshComponent() override;
    bool SetAnimBlueprint(const FString& Path);
    bool ReloadAnimBlueprint();
    UAnimGraphInstance* CreateAnimGraph();
    const FString& GetAnimBlueprintPath() const;
    const FString& GetAnimBlueprintError() const;
	void SetAnimInstance(UAnimInstance* InInstance);
	UAnimSingleNodeInstance* GetSingleNodeInstance() const;
	UAnimInstance* GetAnimInstance() const { return AnimInstance; }
	void BeginPlay() override;
	void TickComponent(float DeltaTime) override;

	const TArray<FBoneInfo>& GetBones() const;
	int32 FindBoneIndex(const FString& Name) const;
	FTransform GetBoneLocalTransform(int32 BoneIndex) const;
	void SetBoneLocalTransform(int32 BoneIndex, const FTransform& Local);
	const TArray<FTransform>& GetBoneSpaceTransforms() const { return BoneSpaceTransforms; }
	void SetBoneSpaceTransforms(const struct FCompactPose& Pose); // 애니메이션 평가 결과를 받는 입구
	FMatrix GetBoneComponentMatrix(int32 BoneIndex) const;
	FMatrix GetBoneWorldMatrix(int32 BoneIndex) const;

	void ResetPose();
	void RefreshBoneTransformsNow();

	using Super::Serialize;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;

	void SetAnimation(UAnimationAsset* NewAnimToPlay);
	void Play(bool bLooping = true);
	void Stop();
	bool IsPlaying() const;
	void PlayAnimation(UAnimationAsset* NewAnimToPlay, bool bLoop = true);
	void RefreshAnimationPose();

	UPROPERTY()
	float BlendSpaceInput = 300.0f;

	// 루트 모션: 이번 Tick까지 모인 이동량(월드 공간). CharacterMovement가 가져가 Actor를 움직인다
	bool HasPendingRootMotion() const { return bHasPendingRootMotion; }
	FTransform ConsumeRootMotion();

	// root 부모 공간 delta → 월드 공간 delta (Unreal ConvertLocalRootMotionToWorld + Armature 변환). 시퀀스 뷰어 미리보기도 쓴다
	FTransform ConvertRootMotionToWorld(const FTransform& ParentSpaceDelta) const;

	// 루트 모션이 뽑히는 본. "root"를 먼저, 없으면 첫 본 (UAnimSequence::FindRootTrack과 같은 규칙)
	int32 GetRootMotionBoneIndex() const;

	// 디버그: root 본 구체 + 액터 위치(root 높이)에서 root까지의 선
	void DrawRootDebug(FLineBatcher* LineBatcher) const;

	TArray<FBodyInstance*> Bodies;
	TArray<FConstraintInstance*>Constraints;
	bool bBlendPhysics = false;

	void InitArticulated(FPhysicsScene* PhysScene);
	void TermArticulated();
	void SetSimulatePhysics(bool bEnabled) override;
	void SetAllBodiesSimulatePhysics(bool bNewSimulate);
	// 이름이 없으면 루트 바디 (UE와 같음)
	FBodyInstance* GetBodyInstance(FName BoneName = NAME_None) const override;
	bool HasValidPhysicsBody() const override;
	UPhysicsAsset* GetPhysicsAsset() const;

	// 래그돌 중 컴포넌트가 따라갈 바디(본 계층에서 가장 위에 있는 바디)와 그 바디 기준 컴포넌트 Transform
	struct FRootBodyData
	{
		int32 BodyIndex = INDEX_NONE;
		FTransform TransformToRoot;
	};
	int32 FindRootBodyIndex() const;
	void SetRootBodyIndex(int32 InBodyIndex);
	void ResetRootBodyIndex();
	FTransform GetComponentTransformFromBodyInstance(FBodyInstance* UseBI) override;

protected:
	// 단일 애니메이션 에셋 경로/루프/재생 속도를 씬에 저장·복원한다 (Serialize에서 부른다)
	void SerializeSingleAnimation(FStructuredArchive::FRecord& Record);

	void RefreshBoneTransforms() override;
	void OnSkeletalMeshChanged() override;

	void OnCreatePhysicsState() override;
	void OnDestroyPhysicsState() override;
	void InstantiatePhysicsAssetBodies_Internal(const UPhysicsAsset& PhysAsset, FPhysicsScene& PhysScene);
	void UpdateKinematicBonesToAnim(); // 애니메이션 상태: 바디가 본을 따라감
	void BlendPhysicsBones(); // 래그돌 상태: 바디->본

	//래그돌 바디가 있을 때만 등록한다
	friend struct FSkeletalMeshComponentEndPhysicsTickFunction;
	FSkeletalMeshComponentEndPhysicsTickFunction EndPhysicsTickFunction;
	void RegisterEndPhysicsTick(bool bRegister);
	void EndPhysicsTickComponent(float DeltaTime);
private:
    UPROPERTY()
    FString AnimBlueprintPath;

    TUniquePtr<UAnimGraphInstance> ManagedAnimGraph;
    FString AnimBlueprintError;
    bool bBlueprintReloadRequested = false;
    bool bUpdatingAnimation = false;

	UPROPERTY()
	UAnimInstance* AnimInstance = nullptr;

	TArray<FTransform> BoneSpaceTransforms;

	FRootBodyData RootBodyData;
	// [Hitori 추가] 래그돌을 켜기 직전 상태. 끌 때 똑바로 세우는 데 쓴다
	FTransform PreRagdollComponentTransform;
	FVector PreRagdollRootBodyLocation = FVector(0, 0, 0);

	FTransform PendingRootMotion;           // 월드 공간 누적값
	bool bHasPendingRootMotion = false;
};
