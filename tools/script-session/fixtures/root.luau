--!strict
local logic = require("0000000000000010")
local function adjusted(amount: number): number
    local extra = logic.extra
    return amount + extra
end
return function(config: Config, state: State, event: Interact, api: Api)
    local amount = adjusted(event.Amount)
    state.Interactions += amount
    if state.Interactions >= config.Threshold and not state.OpenRequested then
        local result = api.RequestDoorOpen(event.Target, event.Amount)
        if result.Status == "Accepted" then
            state.OpenRequested = true
        end
    end
end
