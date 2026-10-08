--!strict
return function(config: Config, state: State, event: Event, api: Api)
    state.Interactions = 1
    api.RequestSignal(true)
    api.RequestDoorOpen(event.Target, 1)
    if event.Amount == 1 then state.Interactions = 1.5
    elseif event.Amount == 2 then state.Interactions = 0/0
    elseif event.Amount == 3 then state.Interactions = 1/0
    elseif event.Amount == 4 then state.Interactions = -1
    elseif event.Amount == 5 then state.Interactions = 4294967296
    elseif event.Amount == 6 then state.Interactions = 101
    elseif event.Amount == 7 then api.RequestDoorOpen(({} :: any), 1)
    else api.RequestSignal((1 :: any)) end
end
