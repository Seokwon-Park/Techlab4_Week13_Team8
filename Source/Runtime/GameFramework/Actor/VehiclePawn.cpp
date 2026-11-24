#include "EnginePCH.h"
#include "GameFramework/Actor/VehiclePawn.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"

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
