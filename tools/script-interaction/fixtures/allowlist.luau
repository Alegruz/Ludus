--!strict
return function(config: Config, state: State, event: Interact, api: Api)
    assert(os == nil and io == nil and debug == nil and require == nil)
    assert(print == nil and getfenv == nil and setfenv == nil)
    assert(pcall == nil and xpcall == nil and loadstring == nil)
end
