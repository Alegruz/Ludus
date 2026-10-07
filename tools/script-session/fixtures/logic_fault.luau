--!strict
local function fail(): number
    error("candidate initializer rejected")
end
return {extra = fail()}
