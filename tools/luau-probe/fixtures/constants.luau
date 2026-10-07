--!strict
-- Exercise constant-table deserialization, including the loader scratch buffer.
local a = { alpha = 1, beta = "two", gamma = true, delta = 4 }
local b = { alpha = 5, beta = "six", gamma = false, delta = 8 }
return a.alpha + b.alpha
