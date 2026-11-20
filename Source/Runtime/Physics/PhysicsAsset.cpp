#include "EnginePCH.h"
#include "PhysicsAsset.h"

void UPhysicsAsset::DisableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	if (BodyIndexA == BodyIndexB) return;
	CollisionDisableTable.Add(FRigidBodyIndexPair(BodyIndexA, BodyIndexB), false);
}

void UPhysicsAsset::EnableCollision(int32 BodyIndexA, int32 BodyIndexB)
{
	CollisionDisableTable.Remove(FRigidBodyIndexPair(BodyIndexA, BodyIndexB));
}

bool UPhysicsAsset::IsCollisionEnabled(int32 BodyIndexA, int32 BodyIndexB) const
{
	return !CollisionDisableTable.Contains(FRigidBodyIndexPair(BodyIndexA, BodyIndexB));
}

int32 UPhysicsAsset::FindBodyIndex(FName BodyName) const
{
	const int32* Found = BodySetupIndexMap.Find(BodyName);
	return Found ? *Found : INDEX_NONE;
}

int32 UPhysicsAsset::FindConstraintIndex(FName Bone1Name, FName Bone2Name) const
{
	for (int32 i = 0; i < ConstraintSetup.Num(); ++i)
	{
		const FConstraintInstance& CI = ConstraintSetup[i]->DefaultInstance;
		if (CI.ConstraintBone1 == Bone1Name && CI.ConstraintBone2 == Bone2Name)
			return i;
	}
	return INDEX_NONE;
}

void UPhysicsAsset::UpdateBodySetupIndexMap()
{
	BodySetupIndexMap.Empty();
	for (int32 i = 0; i < SkeletalBodySetups.Num(); ++i)
		BodySetupIndexMap.Add(SkeletalBodySetups[i]->BoneName, i);
}

void UPhysicsAsset::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();

	int32 NumBodies = Ar.IsSaving() ? SkeletalBodySetups.Num() : 0;
	auto Bodies = Record.EnterArray("SkeletalBodySetups", NumBodies);
	if (Ar.IsLoading()) SkeletalBodySetups.SetNum(NumBodies);
	for (int32 i = 0; i < NumBodies; ++i)
	{
		if (Ar.IsLoading()) SkeletalBodySetups[i] = NewObject<USkeletalBodySetup>(this);
		SkeletalBodySetups[i]->Serialize(Bodies.EnterElement().EnterRecord());
	}

	int NumConstraints = Ar.IsSaving() ? ConstraintSetup.Num() : 0;
	auto Constraints = Record.EnterArray("ConstraintSetup", NumConstraints);
	if (Ar.IsLoading()) ConstraintSetup.SetNum(NumConstraints);
	for (int32 i = 0; i < NumConstraints; ++i)
	{
		if (Ar.IsLoading()) ConstraintSetup[i] = NewObject<UPhysicsConstraintTemplate>(this);
		ConstraintSetup[i]->Serialize(Constraints.EnterElement().EnterRecord());
	}

	// 충돌 끄기 테이블: [{ "A": 0, "B": 2 }, ...]
	TArray<FRigidBodyIndexPair> Pairs;
	if(Ar.IsSaving())
		for(const auto&[Pair, bEnabled] : CollisionDisableTable)
			Pairs.Add(Pair);

	int32 NumPairs = Pairs.Num();
	auto PairArray = Record.EnterArray("CollisionDisableTable", NumPairs);
	if (Ar.IsLoading()) Pairs.SetNum(NumPairs);
	for (int32 i = 0; i < NumPairs; ++i)
	{
		auto R = PairArray.EnterElement().EnterRecord();
		R << SA_VALUE("A", Pairs[i].Indices[0]);
		R << SA_VALUE("B", Pairs[i].Indices[1]);
	}

	if (Ar.IsLoading())
	{
		CollisionDisableTable.Empty();
		for (const FRigidBodyIndexPair& Pair : Pairs)
			DisableCollision(Pair.Indices[0], Pair.Indices[1]);
		UpdateBodySetupIndexMap();
	}
}

void UPhysicsConstraintTemplate::Serialize(FStructuredArchive::FRecord Record)
{
	Super::Serialize(Record);
	FArchive& Ar = Record.GetUnderlyingArchive();
	FConstraintInstance& CI = DefaultInstance;
	FConstraintProfileProperties& P = CI.ProfileInstance;

	FString Bone1 = Ar.IsSaving() ? CI.ConstraintBone1.ToString() : FString{};
	FString Bone2 = Ar.IsSaving() ? CI.ConstraintBone2.ToString() : FString{};
	Record << SA_VALUE("ConstraintBone1", Bone1);
	Record << SA_VALUE("ConstraintBone2", Bone2);

	Record << SA_VALUE("Pos1", CI.Pos1);
	Record << SA_VALUE("PriAxis1", CI.PriAxis1);
	Record << SA_VALUE("SecAxis1", CI.SecAxis1);
	Record << SA_VALUE("Pos2", CI.Pos2);
	Record << SA_VALUE("PriAxis2", CI.PriAxis2);
	Record << SA_VALUE("SecAxis2", CI.SecAxis2);

	int32 TwistMotion = P.TwistLimit.TwistMotion;
	int32 Swing1Motion = P.ConeLimit.Swing1Motion;
	int32 Swing2Motion = P.ConeLimit.Swing2Motion;
	Record << SA_VALUE("TwistMotion", TwistMotion);
	Record << SA_VALUE("Swing1Motion", Swing1Motion);
	Record << SA_VALUE("Swing2Motion", Swing2Motion);
	Record << SA_VALUE("TwistLimitDegrees", P.TwistLimit.TwistLimitDegrees);
	Record << SA_VALUE("Swing1LimitDegrees", P.ConeLimit.Swing1LimitDegrees);
	Record << SA_VALUE("Swing2LimitDegrees", P.ConeLimit.Swing2LimitDegrees);
	Record << SA_VALUE("bDisableCollision", P.bDisableCollision);

	if (Ar.IsLoading())
	{
		CI.ConstraintBone1 = FName(Bone1);
		CI.ConstraintBone2 = FName(Bone2);
		P.TwistLimit.TwistMotion = static_cast<EAngularConstraintMotion>(TwistMotion);
		P.ConeLimit.Swing1Motion = static_cast<EAngularConstraintMotion>(Swing1Motion);
		P.ConeLimit.Swing2Motion = static_cast<EAngularConstraintMotion>(Swing2Motion);
	}
}
