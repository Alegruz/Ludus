--!strict
return function(config: Config, state: State, event: Event, api: Api)
    local text = "safe\0bytes"
    local object = { event.Target }
    local value = 1
    state.Interactions = value
    api.RequestSignal(true)
    assert(text ~= "" and object[1] == event.Target)
end
