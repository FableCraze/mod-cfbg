option(MOD_CFBG "Build cross-faction battleground and Wintergrasp module" ON)
if(NOT MOD_CFBG)
  set(MODULE_ENABLED FALSE)
endif()
