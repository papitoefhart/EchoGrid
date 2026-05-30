cmake_minimum_required(VERSION 3.22)
project(EchoGrid VERSION 1.0.0)

# ── CI mode: set -DECHOGRID_CI=ON to skip copy-after-build and host-specific
#    post-build commands that require a live DAW installation.  GitHub Actions
#    sets this automatically via the workflow. ─────────────────────────────────
option(ECHOGRID_CI "Disable copy-after-build and host-specific post-build steps" OFF)

if(ECHOGRID_CI)
    set(COPY_AFTER_BUILD FALSE)
else()
    set(COPY_AFTER_BUILD TRUE)
endif()

# ── JUCE: local install takes priority; CI clones it into ./JUCE/ via the
#    workflow.  If neither is found, FetchContent downloads it automatically. ──
set(JUCE_LOCAL_PATH "/Users/Johannes/Documents/JUCE")

if(NOT ECHOGRID_CI AND EXISTS "${JUCE_LOCAL_PATH}/CMakeLists.txt")
    # Developer machine — use the existing local installation
    add_subdirectory("${JUCE_LOCAL_PATH}" JUCE)
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    # CI / repo checkout — JUCE was cloned into the project folder
    add_subdirectory(JUCE JUCE)
else()
    # Last resort — download JUCE at configure time
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.12
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# ── Plugin target ─────────────────────────────────────────────────────────────
juce_add_plugin(EchoGrid
    COMPANY_NAME                "Johannes Loeffler Music"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    PLUGIN_AU_MAIN_TYPE         kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD     ${COPY_AFTER_BUILD}
    PLUGIN_MANUFACTURER_CODE    Egrd
    PLUGIN_CODE                 Eg01
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "EchoGrid"
    VERSION                     1.0.0
)

juce_generate_juce_header(EchoGrid)

# ── macOS-only post-build helpers (skipped on CI and Windows) ────────────────
if(APPLE AND NOT ECHOGRID_CI)
    # Close the standalone before rebuilding so the binary is not locked
    add_custom_command(TARGET EchoGrid_Standalone PRE_BUILD
        COMMAND pkill -x "EchoGrid" 2>/dev/null || true
        COMMENT "Closing standalone if running"
    )

    # After every AU build, force macOS to re-register the component so Ableton
    # picks up the new binary with a simple rescan (no cache wipe needed)
    add_custom_command(TARGET EchoGrid_AU POST_BUILD
        COMMAND killall -9 AudioComponentRegistrar 2>/dev/null || true
        COMMAND rm -rf $ENV{HOME}/Library/Caches/AudioUnitCache
        COMMENT "Clearing AU cache — restart Ableton to pick up changes"
    )
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
target_sources(EchoGrid
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginEditor.cpp
        Source/NodeTimeline.cpp
        Source/NodeInspector.cpp
)

target_compile_definitions(EchoGrid
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(EchoGrid
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
