--!strict
local result = host_add(20, 22)
assert(result == 42)
assert(os == nil and io == nil and debug == nil and require == nil and print == nil)
return result
