--!strict
return function(config: Config, state: State, event: Event, api: Api)
    for _ = 1, 17 do api.RequestSignal(true) end
end
