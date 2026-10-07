--!strict
local allowed: {[string]: boolean} = {
    assert = true, error = true, getmetatable = true, ipairs = true, next = true,
    pairs = true, rawequal = true, rawget = true, rawlen = true, select = true,
    tonumber = true, tostring = true, type = true, typeof = true,
    _VERSION = true, _G = true, ["require"] = true, __ludus_api = true,
}
return function(config: Config, state: State, event: Event, api: Api)
    for key in pairs(_G) do assert(allowed[key], "unexpected ambient global") end
    for key in pairs(allowed) do assert(_G[key] ~= nil, "missing reviewed global") end
    assert(_G._G == _G and type(assert) == "function")
    assert(pcall == nil and xpcall == nil and coroutine == nil)
    assert(getmetatable(state) == "locked")
    state.Interactions = 1
    assert(api.RequestSignal(true).Status == "Accepted")
end
