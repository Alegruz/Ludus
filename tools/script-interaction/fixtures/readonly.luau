--!strict
return function(config: Config, state: State, event: Interact, api: Api)
    state.Interactions = 2
    config.Threshold = 3
end
