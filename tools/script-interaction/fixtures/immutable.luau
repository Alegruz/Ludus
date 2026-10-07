--!strict
return function(config: Config, state: State, event: Interact, api: Api)
    state.Interactions = 2
    local result = api.RequestDoorOpen(event.Target, 1)
    if event.Amount == 1 then
        event.Amount = 3
    elseif event.Amount == 2 then
        api.RequestDoorOpen = nil :: any
    else
        result.Status = "Applied"
    end
end
