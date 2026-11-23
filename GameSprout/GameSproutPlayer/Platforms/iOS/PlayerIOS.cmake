set_target_properties(GameSproutPlayer PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_LIST_DIR}/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${GAMESPROUT_IOS_BUNDLE_IDENTIFIER}"
    MACOSX_BUNDLE_BUNDLE_NAME "GameSprout Player"
    MACOSX_BUNDLE_BUNDLE_VERSION 1
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/Product>"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${GAMESPROUT_IOS_BUNDLE_IDENTIFIER}"
    XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@executable_path/Frameworks"
)
if(TARGET GameLogic)
    set_target_properties(GameLogic PROPERTIES
        FRAMEWORK TRUE
        MACOSX_FRAMEWORK_IDENTIFIER "${GAMESPROUT_IOS_BUNDLE_IDENTIFIER}.gamelogic"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${GAMESPROUT_IOS_BUNDLE_IDENTIFIER}.gamelogic"
        XCODE_ATTRIBUTE_SKIP_INSTALL YES
    )
    set_target_properties(GameSproutPlayer PROPERTIES
        XCODE_EMBED_FRAMEWORKS GameLogic
        XCODE_EMBED_FRAMEWORKS_REMOVE_HEADERS_ON_COPY ON
        XCODE_EMBED_FRAMEWORKS_CODE_SIGN_ON_COPY "${GAMESPROUT_IOS_SIGN}"
    )
    add_dependencies(GameSproutPlayer GameLogic)
endif()
# A folder reference keeps Artifact's flat identity-based filenames intact.
# Xcode copies the complete Game directory before signing the bundle.
set_source_files_properties("${GAMESPROUT_IOS_PAYLOAD}" PROPERTIES
    XCODE_EXPLICIT_FILE_TYPE folder
    MACOSX_PACKAGE_LOCATION Resources
)
target_sources(GameSproutPlayer PRIVATE "${GAMESPROUT_IOS_PAYLOAD}")
