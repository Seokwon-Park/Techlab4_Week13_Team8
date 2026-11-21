#pragma once

#include "UObject/Object.h"
#include "Physics/AggregateGeom.h"
#include "BodySetup.generated.h"

enum EPhysicsType : uint8
{
	PhysType_Default,
	PhysType_Kinematic,
	PhysType_Simulated
};

UCLASS()
class UBodySetupCore : public UObject
{
	GENERATED_BODY()

public:
	FName BoneName;
	EPhysicsType PhysicsType = PhysType_Default;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
};

UCLASS()
class UBodySetup : public UBodySetupCore
{
	GENERATED_BODY()

public:
	FKAggregateGeom AggGeom;

	bool bOverrideMass = false;
	float MassInKgOverride = 0.f;
	float Friction = 0.5f;
	float Restitution = 0.1f;

	using Super::Serialize;
	void Serialize(FStructuredArchive::FRecord Record) override;
};

UCLASS()
class USkeletalBodySetup : public UBodySetup
{
	GENERATED_BODY()
};
