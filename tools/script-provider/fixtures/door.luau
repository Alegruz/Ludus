--!strict
local helper = require([=[0000000000000600]=])
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions += event.Amount + helper.Offset
    if state.Interactions >= config.Threshold and not state.OpenRequested then
        local result = api.RequestDoorOpen(event.Target, event.Amount)
        if result.Status == "Accepted" then
            state.OpenRequested = true
        end
    end
end
