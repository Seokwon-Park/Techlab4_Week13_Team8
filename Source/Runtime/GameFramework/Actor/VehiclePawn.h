#pragma once

#include "GameFramework/Pawn.h"
#include <memory>
#include "VehiclePawn.generated.h"

UCLASS()
class AVehiclePawn : public APawn
{
	GENERATED_BODY()
public:
	AVehiclePawn();
	~AVehiclePawn() override;
	void BeginPlay() override;
	void EndPlay() override;

private:
	struct FVehiclePhysics;
	std::unique_ptr<FVehiclePhysics> VehiclePhysics;

	void TickVehiclePhysics(float StepDelta);
};
