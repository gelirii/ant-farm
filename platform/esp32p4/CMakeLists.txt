# Add this directory to EXTRA_COMPONENT_DIRS in the selected board project.
# This is the shared simulation component, not a panel-specific board project.
idf_component_register(
    SRCS "../../src/core.cpp" "../../src/weather.cpp"
         "../../src/persistence.cpp" "../../src/render.cpp"
    INCLUDE_DIRS "../../include")
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
