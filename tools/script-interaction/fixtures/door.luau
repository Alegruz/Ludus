--!strict
return function(config: Config, state: State, event: Interact, api: Api)
    assert(event.Target == event.Alias)
    state.Interactions += event.Amount
    if state.Interactions >= config.Threshold and not state.OpenRequested then
        local result = api.RequestDoorOpen(event.Target, event.Amount)
        if result.Status == "Accepted" then
            state.OpenRequested = true
        end
    end
end
