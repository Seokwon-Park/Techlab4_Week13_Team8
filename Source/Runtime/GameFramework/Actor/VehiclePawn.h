#pragma once

#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include <memory>
#include "VehiclePawn.generated.h"

UCLASS()
class UVehicleWheelMeshComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

protected:
	bool ShouldCreatePhysicsState() const override { return false; }
};

UCLASS()
class AVehiclePawn : public APawn
{
	GENERATED_BODY()
public:
	AVehiclePawn();
	~AVehiclePawn() override;
	void BeginPlay() override;
	void Tick(float DeltaTime) override;
	void EndPlay() override;

private:
	static constexpr uint32 WheelCount = 4;
	UVehicleWheelMeshComponent* WheelMeshes[WheelCount]{};

	struct FVehiclePhysics;
	std::unique_ptr<FVehiclePhysics> VehiclePhysics;

	void TickVehiclePhysics(float StepDelta);
	void SyncWheelMeshes(float StepDelta);
};
