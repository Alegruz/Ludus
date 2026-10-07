--!strict
return function(config: Config, state: State, event: Event, api: Api)
    api.RequestSignal(true)
    api.RequestDoorOpen({} :: any, 1)
end
