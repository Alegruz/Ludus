--!strict
-- Separately authored reference, not generated or round-tripped from the graph.
return function(config: Config, state: State, event: Interact, api: Api)
    state.Interactions += event.Amount
    if state.Interactions >= 2 and not state.OpenRequested then
        local result = api.RequestDoorOpen(event.Target, event.Amount)
        if result.Status == "Accepted" then
            state.OpenRequested = true
        end
    end
end
