cmake_minimum_required(VERSION 3.22)
project(EchoGrid VERSION 1.0.1)

#--- unique build identifier used to stamp the installed AU bundle so macOS
#    and hosts can distinguish rebuilt versions even when the short version
#    string stays the same.
string(TIMESTAMP ECHOGRID_BUILD_ID "%Y%m%d%H%M%S")

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
    BUNDLE_ID                   com.johannes.EchoGrid
    FORMATS                     AU VST3 Standalone
    PRODUCT_NAME                "EchoGrid"
    VERSION                     1.0.1
)

juce_generate_juce_header(EchoGrid)

# ── macOS-only post-build helpers (skipped on CI and Windows) ────────────────
if(APPLE)
    message(STATUS "ECHOGRID_CI=${ECHOGRID_CI}")

    # Close the standalone before rebuilding so the binary is not locked
    add_custom_command(TARGET EchoGrid_Standalone PRE_BUILD
        COMMAND pkill -x "EchoGrid" 2>/dev/null || true
        COMMENT "Closing standalone if running"
    )

    if(NOT ECHOGRID_CI)
        # After every AU build, replace the installed component bundle with the
        # rebuilt one and force macOS to re-register it so Ableton sees the update.
        add_custom_command(TARGET EchoGrid_AU POST_BUILD
            COMMAND /usr/bin/plutil -replace CFBundleVersion -string "1.0.1.${ECHOGRID_BUILD_ID}"
                "$<TARGET_FILE_DIR:EchoGrid_AU>/../Info.plist"
            COMMAND ${CMAKE_COMMAND} -E rm -rf $ENV{HOME}/Library/Audio/Plug-Ins/Components/EchoGrid.component
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "$<TARGET_FILE_DIR:EchoGrid_AU>/../.."
                $ENV{HOME}/Library/Audio/Plug-Ins/Components/EchoGrid.component
            COMMAND /usr/bin/plutil -replace CFBundleIdentifier -string com.johannes.EchoGrid
                "$ENV{HOME}/Library/Audio/Plug-Ins/Components/EchoGrid.component/Contents/Info.plist"
            COMMAND /usr/bin/plutil -replace CFBundleVersion -string "1.0.1.${ECHOGRID_BUILD_ID}"
                "$ENV{HOME}/Library/Audio/Plug-Ins/Components/EchoGrid.component/Contents/Info.plist"
            COMMAND killall -9 AudioComponentRegistrar 2>/dev/null || true
            COMMAND rm -rf $ENV{HOME}/Library/Caches/AudioUnitCache
            COMMENT "Replacing installed AU bundle and clearing cache so the new version is picked up"
        )
    else()
        message(STATUS "Skipping installed AU replacement because ECHOGRID_CI=ON")
    endif()
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
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ── Unit test target for reverse logic (optional) ─────────────────────────────
add_executable(EchoGrid_unit_test
    tests/test_reverse.cpp
)
target_include_directories(EchoGrid_unit_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Source)
target_include_directories(EchoGrid_unit_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/EchoGrid_artefacts/JuceLibraryCode)
target_link_libraries(EchoGrid_unit_test PRIVATE EchoGrid)
target_include_directories(EchoGrid_unit_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/_deps/juce-src/modules)
target_compile_features(EchoGrid_unit_test PRIVATE cxx_std_17)

