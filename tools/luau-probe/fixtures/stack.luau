--!strict
local function recurse(n: number): number
    return 1 + recurse(n + 1)
end
return recurse(0)
