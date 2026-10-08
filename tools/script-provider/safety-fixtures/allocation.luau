--!strict
local constants = { first = true, second = false, third = 2 }
return function(config: Config, state: State, event: Event, api: Api)
    local values = {}
    for i = 1, 2048 do values[i] = { i, constants.third } end
    assert(values[2048][1] == 2048)
    state.Interactions = 1
    assert(api.RequestSignal(true).Status == "Accepted")
end
