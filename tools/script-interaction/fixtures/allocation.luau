--!strict
return function(config: Config, state: State, event: Interact, api: Api)
    state.Interactions = 2
    local result = api.RequestDoorOpen(event.Target, event.Amount)
    assert(result.Status == "Accepted")
    local scratch = {}
    for i = 1, 4096 do scratch[i] = i end
    assert(scratch[4096] == 4096)
end
