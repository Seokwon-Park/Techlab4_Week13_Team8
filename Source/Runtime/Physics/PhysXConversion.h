#pragma once

#include "Math/Transform.h"
#include "PxPhysicsAPI.h"   // premake: Source/ThirdParty/PhysX/include

// UE 4.27 PhysicsCore/Public/PhysXPublicCore.h와 같은 이름. 축 변환 없이 값을 그대로 옮긴다 (UE도 동일)
inline physx::PxVec3 U2PVector(const FVector& V) { return physx::PxVec3(V.X, V.Y, V.Z); }
inline FVector	  P2UVector(const physx::PxVec3& V) { return FVector(V.x, V.y, V.z); }
inline physx::PxQuat U2PQuat(const FQuat& Q) { return physx::PxQuat(Q.X, Q.Y, Q.Z, Q.W); }
inline FQuat		P2UQuat(const physx::PxQuat& Q) { return FQuat(Q.x, Q.y, Q.z, Q.w); }

// PhysX Transform에는 스케일이 없다. 스케일은 Shape 크기에 미리 곱한다
inline physx::PxTransform U2PTransform(const FTransform& T)
{
	return physx::PxTransform(U2PVector(T.Location), U2PQuat(T.Rotation));
}

inline FTransform P2UTransform(const physx::PxTransform& T)
{
	return FTransform(P2UQuat(T.q), P2UVector(T.p));
}

// UE 4.27 BodySetup.cpp:115 — Sphyl(Z축) → PhysX 캡슐(X축). (1,0,1) 축 180° = X·Z 교환 + Y 반전
inline const physx::PxQuat U2PSphylBasis(physx::PxPi, physx::PxVec3(0.70710678f, 0.f, 0.70710678f));

// UE 4.27 PhysicsGeometry.cpp:173 — 캡슐 Shape의 로컬 포즈
inline physx::PxTransform U2PSphylLocalPose(const FVector& Center, const FRotator& Rotation)
{
	return physx::PxTransform(U2PVector(Center), U2PQuat(Rotation.Quaternion()) * U2PSphylBasis);
}