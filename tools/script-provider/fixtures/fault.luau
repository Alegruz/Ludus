--!strict
return function(config: Config, state: State, event: Event, api: Api)
    api.RequestSignal(true)
    state.Interactions = -1
end
