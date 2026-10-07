--!strict
local add = host_add
local result = 0
for i = 1, 10000 do
    result = add(result, 1)
end
return result
