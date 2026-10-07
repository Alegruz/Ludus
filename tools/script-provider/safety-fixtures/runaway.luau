--!strict
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions = 1
    api.RequestSignal(true)
    while true do end
end
