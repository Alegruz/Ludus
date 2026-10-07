--!strict
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions += event.Amount
    api.RequestSignal(state.Interactions >= config.Threshold)
end
