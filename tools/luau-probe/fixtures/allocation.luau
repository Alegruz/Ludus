--!strict
local values = {}
for i = 1, 1024 do
    values[i] = { i, i + 1, i + 2 }
end
return #values
