--!strict
return function(config: Config, state: State, event: Interact, api: Api)
    state.Interactions += event.Amount
    local result = api.RequestDoorOpen(event.Target, event.Amount)
    assert(result.Status == "Accepted")
    state.OpenRequested = true
    if event.Amount == 10 then
        error("fault after staging state and command")
    end
end
