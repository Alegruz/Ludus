--!strict
local previous: State? = nil
return function(config: Config, state: State, event: Interact, api: Api)
    if previous then
        previous.Interactions = 2
    end
    previous = state -- Deliberate violation used only by the acceptance fixture.
end
