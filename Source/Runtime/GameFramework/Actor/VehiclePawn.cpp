#include "EnginePCH.h"
#include "GameFramework/Actor/VehiclePawn.h"

#include "Asset/AssetManager.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/EngineLog.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Physics/BodyInstance.h"
#include "Physics/PhysicsScene.h"
#include "Physics/PhysXConversion.h"
#include "Physics/PhysXSDK.h"

#include <PxPhysicsAPI.h>
#include <vehicle/PxVehicleDrive4W.h>
#include <vehicle/PxVehicleTireFriction.h>
#include <vehicle/PxVehicleUpdate.h>
#include <vehicle/PxVehicleUtil.h>
#include <vehicle/PxVehicleUtilControl.h>

using namespace physx;

namespace
{
	constexpr float WheelRadius = 0.35f;
	constexpr float WheelWidth = 0.20f;
	constexpr float AvanteNMeshScale = 0.01f;
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
	FDelegateHandle PostStepHandle = 0;
	PxVehicleDrive4W* Drive = nullptr;
	PxBatchQuery* BatchQuery = nullptr;
	PxVehicleDrivableSurfaceToTireFrictionPairs* Friction = nullptr;
	PxRaycastQueryResult RayHits[WheelCount]{};
	PxWheelQueryResult WheelResults[WheelCount]{};
	PxRigidDynamic* ChassisBody = nullptr;
	bool bAvanteN = false;
	float OriginalMass = 0.0f;
	PxTransform OriginalCenterOfMass{PxIdentity};
	PxVec3 OriginalInertiaTensor{0.0f};
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
		if (ChassisBody)
		{
			if (bAvanteN)
				ChassisBody->setMass(OriginalMass);
			ChassisBody->setCMassLocalPose(OriginalCenterOfMass);
			ChassisBody->setMassSpaceInertiaTensor(OriginalInertiaTensor);
		}
		if (Friction)
			Friction->release();
	}
};

AVehiclePawn::AVehiclePawn()
{
	UStaticMeshComponent* Chassis = CreateDefaultSubobject<UStaticMeshComponent>("Chassis");
	SetRootComponent(Chassis);
	Chassis->SetSimulatePhysics(true);

	const char* WheelNames[WheelCount] = {"WheelFL", "WheelFR", "WheelRL", "WheelRR"};
	UStaticMesh* Cylinder = UAssetManager::GetAssetByPath<UStaticMesh>("Cylinder");
	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		UVehicleWheelMeshComponent* WheelMesh = CreateDefaultSubobject<UVehicleWheelMeshComponent>(WheelNames[Index]);
		WheelMesh->SetStaticMesh(Cylinder);
		WheelMesh->SetupAttachment(Chassis);
		WheelMesh->bGenerateOverlapEvents = false;
		WheelMesh->SetVisibility(false);
		WheelMeshes[Index] = WheelMesh;
	}

	USpringArmComponent* SpringArm = CreateDefaultSubobject<USpringArmComponent>("SpringArmComponent");
	SpringArm->SetupAttachment(Chassis);
	SpringArm->TargetArmLength = 6.5f;
	SpringArm->TargetOffset = FVector(0.0f, 0.0f, 0.25f);
	SpringArm->SetRelativeRotation({25.0f, 0.0f, 0.0f});
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = true;
	SpringArm->bCameraLagZOnly = true;
	SpringArm->CameraLagSpeed = 12.0f;
	SpringArm->bEnableCameraRotationLag = true;
	SpringArm->bDrawDebug = false;

	UCameraComponent* Camera = CreateDefaultSubobject<UCameraComponent>("CameraComponent");
	Camera->SetupAttachment(SpringArm);
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

	const UStaticMesh* ChassisMesh = Chassis->GetStaticMesh();
	const bool bAvanteN = ChassisMesh && ChassisMesh->GetPath().ends_with("/AvanteN_Body_Static.fbx.bin");
	State->bAvanteN = bAvanteN;
	State->ChassisBody = Body;
	State->OriginalMass = Body->getMass();
	State->OriginalCenterOfMass = Body->getCMassLocalPose();
	State->OriginalInertiaTensor = Body->getMassSpaceInertiaTensor();
	if (bAvanteN && !PxRigidBodyExt::setMassAndUpdateInertia(*Body, 1450.0f))
		return;

	PxVehicleWheelsSimData* Wheels = PxVehicleWheelsSimData::allocate(WheelCount);
	Wheels->setChassisMass(Body->getMass());

	PxVehicleWheelData Wheel;
	Wheel.mRadius = WheelRadius;
	Wheel.mWidth = bAvanteN ? 0.23f : WheelWidth;
	Wheel.mMOI = 0.5f * Wheel.mMass * Wheel.mRadius * Wheel.mRadius;

	const float X = bAvanteN ? 1.38677f : Box.halfExtents.x * 0.8f;
	const float Y = bAvanteN ? 0.80915f : Box.halfExtents.y + Wheel.mWidth * 0.5f;
	const float Z = bAvanteN ? -0.39166f : -Box.halfExtents.z;
	const PxVec3 WheelPositions[] = {
		{ X, -Y, Z}, { X, Y, Z}, // 앞 왼쪽, 앞 오른쪽
		{-X, -Y, Z}, {-X, Y, Z}  // 뒤 왼쪽, 뒤 오른쪽
	};

	const PxVec3 ShapeCenter = Shape->getLocalPose().p;
	const PxVec3 WheelOrigin = bAvanteN ? PxVec3(0.0f) : ShapeCenter;
	PxTransform CenterOfMass = Body->getCMassLocalPose();
	CenterOfMass.p.z = ShapeCenter.z - Box.halfExtents.z * 0.7f;
	Body->setCMassLocalPose(CenterOfMass);

	PxVec3 InertiaTensor = Body->getMassSpaceInertiaTensor();
	InertiaTensor.y *= 2.0f;
	Body->setMassSpaceInertiaTensor(InertiaTensor);

	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const PxVec3 WheelPosition = WheelPositions[Index] + WheelOrigin;
		const PxVec3 Offset = WheelPosition - CenterOfMass.p;
		const bool bFrontWheel =
			Index == PxVehicleDrive4WWheelOrder::eFRONT_LEFT ||
			Index == PxVehicleDrive4WWheelOrder::eFRONT_RIGHT;
		Wheel.mMaxSteer = bFrontWheel ? PxPi / 5.0f : 0.0f;
		Wheel.mMaxHandBrakeTorque = bFrontWheel ? 0.0f : 3000.0f;
		Wheels->setWheelData(Index, Wheel);
		Wheels->setTireData(Index, PxVehicleTireData{});
		Wheels->setWheelCentreOffset(Index, Offset);
		Wheels->setSuspTravelDirection(Index, PxVec3(0.0f, 0.0f, -1.0f));
		Wheels->setSuspForceAppPointOffset(Index, Offset);
		Wheels->setTireForceAppPointOffset(Index, Offset);
		Wheels->setWheelShapeMapping(Index, -1);
		State->WheelResults[Index].localPose = PxTransform(WheelPosition);
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

	State->Drive->mDriveDynData.forceGearChange(PxVehicleGearsData::eFIRST);
	State->Drive->mDriveDynData.setUseAutoGears(true);

	State->ChassisShape = Shape;
	State->OriginalQueryFilter = Shape->getQueryFilterData();
	PxFilterData QueryFilter = State->OriginalQueryFilter;
	QueryFilter.word3 |= VehicleChassisQueryBit;
	Shape->setQueryFilterData(QueryFilter);

	State->PreStepHandle = Scene->OnPreStep.AddDynamic(this, &AVehiclePawn::TickVehiclePhysics);
	if (State->PreStepHandle != 0)
	{
		State->PostStepHandle = Scene->OnPostStep.AddDynamic(this, &AVehiclePawn::SyncWheelMeshes);
		VehiclePhysics = std::move(State);
	}
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
	const APlayerController* Controller = GetController();
	const auto IsDown = [Controller](EKeyCode Key) {
		return Controller && Controller->IsKeyDown(static_cast<int32>(Key));
	};

	const bool bForward = IsDown(EKeyCode::W);
	const bool bReverse = IsDown(EKeyCode::S);
	const bool bHandbrake = IsDown(EKeyCode::Space);
	const bool bBrake = bForward && bReverse;
	const bool bWantsDrive = (bForward != bReverse) && !bHandbrake;

	constexpr float ShiftSpeedLimit = 0.25f; // m/s
	const float ForwardSpeed = State.Drive->computeForwardSpeed();
	auto& DriveData = State.Drive->mDriveDynData;
	const bool bInReverse = DriveData.getTargetGear() == PxVehicleGearsData::eREVERSE;
	const bool bChangingDirection = bWantsDrive && (bReverse != bInReverse);
	if (bChangingDirection && PxAbs(ForwardSpeed) <= ShiftSpeedLimit)
	{
		DriveData.forceGearChange(
			bReverse ? PxVehicleGearsData::eREVERSE : PxVehicleGearsData::eFIRST);
	}

	const bool bOppositeMotion =
		bReverse ? ForwardSpeed > ShiftSpeedLimit : ForwardSpeed < -ShiftSpeedLimit;
	const bool bBrakeBeforeDrive = bWantsDrive &&
		(bOppositeMotion || (bChangingDirection && PxAbs(ForwardSpeed) > ShiftSpeedLimit));

	PxVehicleDrive4WRawInputData Input;
	Input.setDigitalAccel(bWantsDrive && !bBrakeBeforeDrive);
	Input.setDigitalBrake(bBrake || bBrakeBeforeDrive);
	Input.setDigitalHandbrake(bHandbrake);
	Input.setDigitalSteerLeft(IsDown(EKeyCode::A));
	Input.setDigitalSteerRight(IsDown(EKeyCode::D));

	// 가속, 브레이크, 핸드브레이크, 좌회전, 우회전 순서
	static const PxVehicleKeySmoothingData Smoothing{
		{6.0f, 6.0f, 6.0f, 2.5f, 2.5f},
		{10.0f, 10.0f, 10.0f, 5.0f, 5.0f}
	};
	// 전진 속도(m/s)에 따른 최대 조향 입력 비율
	static const PxReal SteerPairs[] = {
		0.0f, 1.00f,
		3.0f, 0.85f,
		6.0f, 0.52f,
		10.0f, 0.24f,
		18.0f, 0.05f
	};
	static const PxFixedSizeLookupTable<8> SteerBySpeed(SteerPairs, 5);
	PxVehicleDrive4WSmoothDigitalRawInputsAndSetAnalogInputs(
		Smoothing, SteerBySpeed, Input, StepDelta, PxVehicleIsInAir(Results), *State.Drive);

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

void AVehiclePawn::SyncWheelMeshes(float)
{
	const float ChassisScale = GetRootComponent()->GetWorldScale3D().X;
	const bool bAvanteN = VehiclePhysics->bAvanteN;
	const PxQuat MeshBasis = bAvanteN ? PxQuat(PxIdentity) : PxQuat(-PxHalfPi, PxVec3(1.0f, 0.0f, 0.0f));
	const FVector MeshScale = bAvanteN
		? FVector(AvanteNMeshScale, AvanteNMeshScale, AvanteNMeshScale)
		: FVector(WheelRadius, WheelRadius, WheelWidth);

	for (PxU32 Index = 0; Index < WheelCount; ++Index)
	{
		const PxTransform& Pose = VehiclePhysics->WheelResults[Index].localPose;
		WheelMeshes[Index]->SetTransform(FTransform(
			P2UQuat(Pose.q * MeshBasis),
			P2UVector(Pose.p) / ChassisScale,
			MeshScale / ChassisScale));
		WheelMeshes[Index]->SetVisibility(true);
	}
}

void AVehiclePawn::EndPlay()
{
	if (VehiclePhysics)
	{
		FPhysicsScene& Scene = *VehiclePhysics->Scene;
		Scene.OnPreStep.Remove(VehiclePhysics->PreStepHandle);
		Scene.OnPostStep.Remove(VehiclePhysics->PostStepHandle);
		PxSceneWriteLock Lock(*Scene.GetPxScene());
		VehiclePhysics.reset();
	}

	Super::EndPlay();
}
