#include "EnginePCH.h"
#include "GameFramework/Actor/VehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EngineLog.h"
#include "Engine/World.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXSDK.h"

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vehicle/PxVehicleUpdate.h>

using namespace physx;

namespace
{
	constexpr PxU32 WheelCount = 4;
	constexpr PxU32 VehicleChassisQueryBit = 1u << 31;

	PxQueryHitType::Enum FilterSuspensionHit(
		PxFilterData, PxFilterData ShapeFilter, const void*, PxU32, PxHitFlags&)
	{
		if (ShapeFilter.word3 & VehicleChassisQueryBit)
			return PxQueryHitType::eNONE;

		return PxQueryHitType::eBLOCK;
	}
}

struct AVehiclePawn::FVehiclePhysics
{
	FPhysicsScene* Scene = nullptr;
	FDelegateHandle PreStepHandle = 0;
	PxVehicleDrive4W* Drive = nullptr;
	PxBatchQuery* BatchQuery = nullptr;
	PxVehicleDrivableSurfaceToTireFrictionPairs* Friction = nullptr;
	PxRaycastQueryResult RayHits[WheelCount]{};
	PxWheelQueryResult WheelResults[WheelCount]{};
	PxShape* ChassisShape = nullptr;
	PxFilterData OriginalQueryFilter;
	int32 LastGroundedWheels = -1;

	~FVehiclePhysics()
	{
		if (ChassisShape)
			ChassisShape->setQueryFilterData(OriginalQueryFilter);
		if (BatchQuery)
			BatchQuery->release();
		if (Drive)
			Drive->free();
		if (Friction)
			Friction->release();
	}
};

AVehiclePawn::AVehiclePawn()
{
	UStaticMeshComponent* Chassis = CreateDefaultSubobject<UStaticMeshComponent>("Chassis");
	SetRootComponent(Chassis);
	Chassis->SetSimulatePhysics(true);

	UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	Camera->SetupAttachment(Chassis);
	Camera->SetRelativeLocation({-6.0f, 0.0f, 3.0f});
	Camera->SetRelativeRotation({25.0f, 0.0f, 0.0f});
	Camera->SetExternalInputManaged(true);
}

AVehiclePawn::~AVehiclePawn() = default;

void AVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	if (!HasBegunPlay() || VehiclePhysics)
		return;

	FPhysicsScene* Scene = GetWorld()->GetPhysicsScene();
	UStaticMeshComponent* Chassis = Cast<UStaticMeshComponent>(GetRootComponent());
	PxRigidDynamic* Body = Chassis ? Chassis->GetBodyInstance()->GetRigidDynamic() : nullptr;
	if (!Scene || !Scene->GetPxScene() || !Body)
		return;

	PxSceneWriteLock Lock(*Scene->GetPxScene());

	PxShape* Shape = nullptr;
	PxBoxGeometry Box;
	PxMaterial* Material = nullptr;
	if (Body->getNbShapes() != 1 || Body->getShapes(&Shape, 1) != 1 ||
		!Shape->getBoxGeometry(Box) || Shape->getMaterials(&Material, 1) != 1)
	{
		HTR_LOG(Warning, "Vehicle Pawn requires a single box chassis");
		return;
	}

	auto State = std::make_unique<FVehiclePhysics>();
	State->Scene = Scene;

	PxBatchQueryDesc QueryDesc(WheelCount, 0, 0);
	QueryDesc.preFilterShader = FilterSuspensionHit;
	QueryDesc.queryMemory.userRaycastResultBuffer = State->RayHits;
	State->BatchQuery = Scene->GetPxScene()->createBatchQuery(QueryDesc);
	State->Friction = PxVehicleDrivableSurfaceToTireFrictionPairs::allocate(1, 1);
	if (!State->BatchQuery || !State->Friction)
		return;

	const PxMaterial* SurfaceMaterials[] = {Material};
	const PxVehicleDrivableSurfaceType SurfaceTypes[] = {{0}};
	State->Friction->setup(1, 1, SurfaceMaterials, SurfaceTypes);

	PxVehicleWheelsSimData* Wheels = PxVehicleWheelsSimData::allocate(WheelCount);
	Wheels->setChassisMass(Body->getMass());

	PxVehicleWheelData Wheel;
	Wheel.mRadius = 0.35f;
	Wheel.mWidth = 0.20f;
	Wheel.mMOI = 0.5f * Wheel.mMass * Wheel.mRadius * Wheel.mRadius;

	const float X = Box.halfExtents.x * 0.8f;
	const float Y = Box.halfExtents.y + Wheel.mWidth * 0.5f;
	const float Z = -Box.halfExtents.z;
	const PxVec3 WheelPositions[] = {
		{ X, -Y, Z}, { X, Y, Z}, // 앞 왼쪽, 앞 오른쪽
		{-X, -Y, Z}, {-X, Y, Z}  // 뒤 왼쪽, 뒤 오른쪽
	};

	const PxVec3 CenterOffset = Shape->getLocalPose().p - Body->getCMassLocalPose().p;
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const PxVec3 Offset = WheelPositions[Index] + CenterOffset;
		Wheels->setWheelData(Index, Wheel);
		Wheels->setTireData(Index, PxVehicleTireData{});
		Wheels->setWheelCentreOffset(Index, Offset);
		Wheels->setSuspTravelDirection(Index, PxVec3(0.0f, 0.0f, -1.0f));
		Wheels->setSuspForceAppPointOffset(Index, Offset);
		Wheels->setTireForceAppPointOffset(Index, Offset);
		Wheels->setWheelShapeMapping(Index, -1);
	}

	PxVehicleAckermannGeometryData Steering;
	Steering.mAxleSeparation = 2.0f * X;
	Steering.mFrontWidth = 2.0f * Y;
	Steering.mRearWidth = 2.0f * Y;

	PxVehicleDriveSimData4W DriveSettings;
	DriveSettings.setEngineData(PxVehicleEngineData{});
	DriveSettings.setGearsData(PxVehicleGearsData{});
	DriveSettings.setClutchData(PxVehicleClutchData{});
	DriveSettings.setAutoBoxData(PxVehicleAutoBoxData{});
	DriveSettings.setDiffData(PxVehicleDifferential4WData{});
	DriveSettings.setAckermannGeometryData(Steering);
	State->Drive = PxVehicleDrive4W::create(
		PhysXSDK::GetPhysics(), Body, *Wheels, DriveSettings, 0);
	Wheels->free();
	if (!State->Drive)
		return;

	State->ChassisShape = Shape;
	State->OriginalQueryFilter = Shape->getQueryFilterData();
	PxFilterData QueryFilter = State->OriginalQueryFilter;
	QueryFilter.word3 |= VehicleChassisQueryBit;
	Shape->setQueryFilterData(QueryFilter);

	State->PreStepHandle = Scene->OnPreStep.AddDynamic(this, &AVehiclePawn::TickVehiclePhysics);
	if (State->PreStepHandle != 0)
		VehiclePhysics = std::move(State);
}

void AVehiclePawn::TickVehiclePhysics(float StepDelta)
{
	FVehiclePhysics& State = *VehiclePhysics;
	PxScene& Scene = *State.Scene->GetPxScene();
	PxSceneWriteLock Lock(Scene);

	const PxVec3 Gravity = Scene.getGravity();
	if (Gravity.magnitudeSquared() == 0.0f)
		return;

	PxVehicleWheels* Vehicles[] = {State.Drive};
	PxVehicleSuspensionRaycasts(State.BatchQuery, 1, Vehicles, WheelCount, State.RayHits);

	// 잠든 차체는 PxVehicleUpdates가 바퀴 결과를 갱신하지 않는다.
	PxVehicleWheelQueryResult Results{State.WheelResults, WheelCount};
	PxVehicleUpdates(StepDelta, Gravity, *State.Friction, 1, Vehicles, &Results);

	int32 GroundedWheels = 0;
	for (const PxWheelQueryResult& Wheel : State.WheelResults)
		GroundedWheels += Wheel.isInAir ? 0 : 1;

	if (GroundedWheels != State.LastGroundedWheels)
	{
		State.LastGroundedWheels = GroundedWheels;
		HTR_LOG(Info, "Vehicle grounded wheels: {}/4", GroundedWheels);
	}
}

void AVehiclePawn::EndPlay()
{
	if (VehiclePhysics)
	{
		FPhysicsScene& Scene = *VehiclePhysics->Scene;
		Scene.OnPreStep.Remove(VehiclePhysics->PreStepHandle);
		PxSceneWriteLock Lock(*Scene.GetPxScene());
		VehiclePhysics.reset();
	}

	Super::EndPlay();
}
