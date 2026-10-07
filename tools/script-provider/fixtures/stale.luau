--!strict
local previous: State? = nil
return function(config: Config, state: State, event: Event, api: Api)
    if previous then
        previous.Interactions += event.Amount
    end
    previous = state
end
