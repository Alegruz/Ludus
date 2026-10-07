--!strict
return function(config: Config, state: State, event: Interact, api: Api)
    local result = api.RequestDoorOpen(event.Target, event.Amount)
    assert(result.Status ~= "Accepted" and result.Token == 0)
end
