--!strict
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions += event.Amount
end
