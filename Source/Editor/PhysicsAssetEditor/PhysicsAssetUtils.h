#pragma once

class UPhysicsAsset;
class USkeletalMesh;

namespace FPhysicsAssetUtils
{
	// 본마다 캡슐 바디 하나, 바디가 있는 가장 가까운 조상과 조인트 하나를 만든다
	// PhysicsAsset은 비어 있어야 한다. 바디를 하나도 못 만들면 false
	bool CreateFromSkeletalMesh(UPhysicsAsset& PhysicsAsset, const USkeletalMesh& Mesh);
}
