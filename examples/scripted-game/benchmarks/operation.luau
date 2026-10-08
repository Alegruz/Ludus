--!strict
return function(config: Config, state: State, event: Event, api: Api)
    api.RequestDoorOpen(event.Target, event.Amount)
end
