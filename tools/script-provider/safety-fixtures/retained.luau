--!strict
local previous: State? = nil
return function(config: Config, state: State, event: Event, api: Api)
    if previous then previous.Interactions = 2 end
    previous = state
    state.Interactions = 1
    api.RequestSignal(true)
end
