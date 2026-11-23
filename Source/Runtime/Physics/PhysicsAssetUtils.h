#pragma once

#include "Core/NameTypes.h"
#include "Physics/ConstraintInstance.h"

class UPhysicsAsset;
class UPhysicsConstraintTemplate;
class USkeletalMesh;

enum EPhysAssetFitGeomType :uint8
{
	EFG_Box,
	EFG_Sphyl,
	EFG_Sphere,
	EFG_SingleConvexHull
};

struct FPhysAssetCreateParams
{
	float MinBoneSize = 20.0f;
	EPhysAssetFitGeomType GeomType = EFG_Sphyl;
	bool bCreateConstraints = true;
	bool bWalkPastSmall = true;
	EAngularConstraintMotion AngularConstraintMode = ACM_Limited;
};

namespace FPhysicsAssetUtils
{
	bool CreateFromSkeletalMesh(UPhysicsAsset* PhysicsAsset, USkeletalMesh* SkelMesh, const FPhysAssetCreateParams& Params, FString& OutErrorMessage, bool bSetToMesh = true);
	int32 CreateNewBody(UPhysicsAsset* PhysAsset, FName InBodyName, const FPhysAssetCreateParams& Params);
	void  DestroyBody(UPhysicsAsset* PhysAsset, int32 BodyIndex);		  // 연결된 Constraint도 제거
	int32 CreateNewConstraint(UPhysicsAsset* PhysAsset, FName InConstraintName,UPhysicsConstraintTemplate* InConstraintSetup = nullptr);
	void  DestroyConstraint(UPhysicsAsset* PhysAsset, int32 ConstraintIndex);
}
