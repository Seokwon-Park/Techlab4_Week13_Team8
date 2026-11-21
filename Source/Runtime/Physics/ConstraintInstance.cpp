#include "EnginePCH.h"
#include "ConstraintInstance.h"

FTransform FConstraintInstance::GetRefFrame(int32 Frame) const
{
    const bool bFrame1 = Frame == 0;
    const FVector Pri = (bFrame1 ? PriAxis1 : PriAxis2).Normalized();
    const FVector Sec = (bFrame1 ? SecAxis1 : SecAxis2).Normalized();
    const FVector& Pos = bFrame1 ? Pos1 : Pos2;

    FMatrix M = FMatrix::Identity;
    M.SetAxis(0, Pri);
    M.SetAxis(1, Sec);
    M.SetAxis(2, Pri.Cross(Sec));
    M[3][0] = Pos.X;
    M[3][1] = Pos.Y;
    M[3][2] = Pos.Z;
    return FTransform::FromMatrix(M);
}
