--!strict
local function recurse(): number return recurse() + 1 end
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions = 1
    api.RequestSignal(true)
    recurse()
end
