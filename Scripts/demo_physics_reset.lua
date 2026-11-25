-- R 키를 누르면 이 액터의 메시를 BeginPlay 때 위치와 물리 상태로 되돌린다.
-- 물리 시연 씬(Scenes/demo)의 Dynamic 물체와 래그돌에 붙인다.

local Saved = {}

function BeginPlay()
    for _, Name in ipairs({ "UStaticMeshComponent", "USkeletalMeshComponent" }) do
        local Comp = GetComponentByName(Name)
        if Comp then
            table.insert(Saved, { Comp = Comp, Location = Comp.Location, Rotation = Comp.Rotation, Simulate = Comp.SimulatePhysics })
        end
    end
end

function Tick(dt)
    if not IsKeyPressed("R") then return end
    for _, S in ipairs(Saved) do
        S.Comp.SimulatePhysics = S.Simulate
        S.Comp:ResetPose()
        S.Comp.Location = S.Location
        S.Comp.Rotation = S.Rotation
        S.Comp:RecreatePhysicsState()
    end
end
