project(NotionClassic)
cmake_minimum_required(VERSION 3.23)


set(CMAKE_C_STANDARD 99)

# See: https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
SET(CMAKE_C_FLAGS   "-Ofast")
SET(CMAKE_CXX_FLAGS "-Ofast")

# Set SSL_ENABLED to OFF if you don't require https communication
SET(SSL_ENABLED ON)

# ADD_DEFINITIONS (GASON_DEBUG_ALLOCATOR="ON")
# If debugging is enabled in the mbedtls library, then enable debugging here
# ADD_DEFINITIONS (-DMBEDTLS_DEBUG="ON")


add_library(MacHTTP STATIC
        HttpClient.cpp
        HttpResponse.h
        HttpResponse.cpp
        Uri.h
        Uri.cpp
        RootCert.h
        http_parser.h
        http_parser.c
        yuarel.c
        yuarel.h
        gason.cpp
        gason.hpp
        net_sockets_mac.c
        uECC.c
)


# Compile only the uECC files for 68020 to unlock native MULS.L/MULU.L.
# On SE/30 (68030) this makes uECC_shared_secret ~5x faster.
# The optimization-level and curve defines must be passed as -D flags so
# that uECC.c (compiled as a separate translation unit) sees the same
# settings as the #defines in uecc_alt.c — otherwise uECC.c defaults to
# uECC_OPTIMIZATION_LEVEL=0 (unoptimized binary double-and-add) and the
# handshake takes ~24 s instead of ~8 s.
set(UECC_FLAGS "-m68020 \
 -DuECC_OPTIMIZATION_LEVEL=2 \
 -DuECC_SQUARE_FUNC=1 \
 -DuECC_WORD_SIZE=4 \
 -DuECC_SUPPORTS_secp256r1=1 \
 -DuECC_SUPPORTS_secp160r1=0 \
 -DuECC_SUPPORTS_secp192r1=0 \
 -DuECC_SUPPORTS_secp224r1=0 \
 -DuECC_SUPPORTS_secp256k1=0 \
 -DuECC_VLI_NATIVE_LITTLE_ENDIAN=0")
set_source_files_properties(uECC.c uecc_alt.c PROPERTIES COMPILE_FLAGS "${UECC_FLAGS}")

SET(MBEDTLS_DIR "/Users/lin/Developer/Retro68/CLionProjects/mbedtls-Mac-68k-master")
SET(MACTCPHELPER_DIR "/Users/lin/Developer/Retro68/CLionProjects/MacTCPHelper-master")

IF(${SSL_ENABLED})
    ADD_DEFINITIONS (-DSSL_ENABLED)
    include_directories(${MBEDTLS_DIR}/include)
    include_directories(/Users/lin/Developer/Retro68/CLionProjects)
    target_link_libraries( MacHTTP
        ${MBEDTLS_DIR}/build/library/libmbedtls.a
        ${MBEDTLS_DIR}/build/library/libmbedx509.a
        ${MBEDTLS_DIR}/build/library/libmbedcrypto.a
        ${MACTCPHELPER_DIR}/build/libMacTCPHelper.a )
ELSE()
    target_link_libraries( MacHTTP ${MACTCPHELPER_DIR}/build/libMacTCPHelper.a )
ENDIF()

# Some options to make things smaller
set_target_properties(MacHTTP PROPERTIES COMPILE_OPTIONS -ffunction-sections)
set_target_properties(MacHTTP PROPERTIES LINK_FLAGS "-Wl,-gc-sections")


add_application(NotionClassic
        main.cpp
        Settings.cpp
        fast_alloc.c
        uecc_alt.c
        emoji_sprite.c
        emoji_names.c
        #size.r
        wdefshell.r
)

target_link_libraries( NotionClassic MacHTTP )
set_target_properties(NotionClassic PROPERTIES LINK_FLAGS "-Wl,--allow-multiple-definition")

# Set hasBundle flag (0x2000) in AppleDouble Finder info so Finder uses the BNDL icon
add_custom_command(TARGET NotionClassic_APPL POST_BUILD
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/set_has_bundle.py" "${CMAKE_CURRENT_BINARY_DIR}/%NotionClassic.ad"
    COMMENT "Setting hasBundle flag on NotionClassic.APPL"
)

# Copy the icon folder next to the built app so GetProcessInformation can find it
add_custom_command(TARGET NotionClassic_APPL POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "${CMAKE_CURRENT_BINARY_DIR}/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_BINARY_DIR}/assets/emoji_sprite.pict"
        "${CMAKE_CURRENT_BINARY_DIR}/assets/emoji_sprite.pict"
    COMMENT "Copying assets folder to build directory"
)

install(TARGETS MacHTTP
    DESTINATION "lib"
)

install(FILES HttpClient.h HttpResponse.h Uri.h http_parser.h
    DESTINATION "include/machttp"
)
