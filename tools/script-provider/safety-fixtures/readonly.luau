--!strict
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions = 1
    api.RequestSignal(true)
    if event.Amount == 1 then config.Threshold = 2
    elseif event.Amount == 2 then event.Amount = 2
    elseif event.Amount == 3 then api.RequestSignal = function(_: boolean): CommandResult return { Status = "Accepted", Token = 1 } end
    else _G["ambient"] = true end
end
