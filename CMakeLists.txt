cmake_minimum_required(VERSION 3.14)
project(torx-ncurses LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_TLS_VERIFY TRUE)

include(GNUInstallDirs)

if(DEFINED ENV{BUILD_SHARED_LIBS} AND NOT DEFINED BUILD_SHARED_LIBS)
	set(BUILD_SHARED_LIBS $ENV{BUILD_SHARED_LIBS})
elseif(NOT DEFINED BUILD_SHARED_LIBS) # Set default as static. Static works, shared works other than libcrypto.a (which we statically compile on android/linux).
	set(BUILD_SHARED_LIBS 0)
endif()
message(STATUS "TorX shared libs: ${BUILD_SHARED_LIBS}")

if(DEFINED ENV{BUILD_TORX_DEPS} AND NOT DEFINED BUILD_TORX_DEPS)
	set(BUILD_TORX_DEPS $ENV{BUILD_TORX_DEPS})
elseif(NOT DEFINED BUILD_TORX_DEPS) # Set default
	set(BUILD_TORX_DEPS 0)
endif()
message(STATUS "TorX dependencies: ${BUILD_TORX_DEPS}")

if(DEFINED ENV{TORX_TAG} AND NOT DEFINED TORX_TAG)
	set(TORX_TAG $ENV{TORX_TAG})
elseif(NOT DEFINED TORX_TAG) # Set default
	set(TORX_TAG "main")
endif()
message(STATUS "TorX tag: ${TORX_TAG}")

if(DEFINED ENV{LAN_REPO} AND NOT DEFINED LAN_REPO)
	set(LAN_REPO $ENV{LAN_REPO})
	set(REPO_TORX "${LAN_REPO}/libtorx.git")
elseif(NOT DEFINED LAN_REPO) # Set default
	set(REPO_TORX "https://github.com/TorX-Chat/libtorx/")
else()
	set(REPO_TORX "${LAN_REPO}/libtorx.git")
endif()
message(STATUS "TorX repository: ${LAN_REPO}")

## START BLOCK: Try to keep this the same in torx-gtk4, torx-ncurses, and libtorx. XXX It WILL be passed to libtorx, but libtorx also has a default in case of independent build. XXX
	#### C90 crap: -Wvla -Wdeclaration-after-statement
	#### Unimportant: -Waggregate-return
	#### Heavily CPU + RAM + time intensive junk static analyzer that throws mainly "possible null dereference" warnings (requires 9+gb ram): -fanalyzer
	#### other static analyzer binaries: cppcheck clang-tidy
if(NOT DEFINED CMAKE_C_FLAGS OR CMAKE_C_FLAGS STREQUAL "")
	## XXX -mshstk is x86 only and a HARD ERROR on other architectures, so it must stay probed, never hardcoded. -mbranch-protection=standard is the ARM equivalent.
	include(CheckCCompilerFlag)
	check_c_compiler_flag("-mshstk" TORX_HAVE_MSHSTK)
	if(TORX_HAVE_MSHSTK)
		set(TORX_CFI_FLAG "-mshstk")
	else()
		check_c_compiler_flag("-mbranch-protection=standard" TORX_HAVE_BRANCH_PROTECTION)
		if(TORX_HAVE_BRANCH_PROTECTION)
			set(TORX_CFI_FLAG "-mbranch-protection=standard")
		else()
			set(TORX_CFI_FLAG "")
			message(STATUS "${PROJECT_NAME} no control-flow protection flag available for this compiler/architecture")
		endif()
	endif()
	if (CMAKE_BUILD_TYPE STREQUAL "Debug") ## XXX Dev builds can add either of: -fsanitize=thread -fsanitize=address
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=0 -O0")
		if(NOT WIN32)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero")
		endif()
	else()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2 -O2")
	endif()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fwrapv ${TORX_CFI_FLAG} -Wstrict-overflow=5 -Wshadow -Wpedantic -Wcast-qual -Wno-overlength-strings -Wwrite-strings -Wnull-dereference -Wstrict-prototypes -Wstrict-aliasing -Wmissing-prototypes -Wmissing-declarations -fstack-protector-strong -Wconversion -Wformat-security")
	## XXX Match the compiler ID, not the path (/usr/bin/cc is clang on macOS). MATCHES not STREQUAL: Apple's toolchain reports AppleClang.
	if(CMAKE_C_COMPILER_ID MATCHES "Clang")
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wcast-align -Weverything -Wno-missing-variable-declarations -Wno-declaration-after-statement -Wno-cast-function-type-strict -Wno-reserved-identifier -Wno-padded -Wno-vla -Wno-used-but-marked-unused -Wno-disabled-macro-expansion -Wno-documentation -Wno-documentation-unknown-command -Wno-double-promotion -Wno-unsafe-buffer-usage -Wno-pre-c11-compat -Wno-switch-enum -Wno-covered-switch-default -Wno-implicit-void-ptr-cast -Wno-jump-misses-init")
	else() # GCC
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wcast-align=strict -Wformat-signedness") # Dropped -fwhole-program because it caused linking errors when passed to libtorx. Likely responsible for other linking abnormalities.
	endif()
endif()
message(STATUS "${PROJECT_NAME} C_FLAGS being set to: ${CMAKE_C_FLAGS}")
## END BLOCK

set(BEEP_WAV ${CMAKE_CURRENT_SOURCE_DIR}/other/beep.wav)
set(BEEP_H ${CMAKE_CURRENT_BINARY_DIR}/beep.h)

find_program(XXD xxd REQUIRED)

add_custom_command(
	OUTPUT ${BEEP_H}
	COMMAND ${XXD} -i -n beep_wav beep.wav ${BEEP_H}
	DEPENDS ${BEEP_WAV}
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/other
	VERBATIM
)

## XXX Opt-in ONLY. Silently preferring an installed libtorx makes TORX_TAG a lie and, because find_* results are cached, keeps reporting a hit long after the installed header is deleted.
if(DEFINED ENV{TORX_SYSTEM} AND NOT DEFINED TORX_SYSTEM)
	set(TORX_SYSTEM $ENV{TORX_SYSTEM})
elseif(NOT DEFINED TORX_SYSTEM) # Set default
	set(TORX_SYSTEM 0)
endif()
message(STATUS "TorX system library: ${TORX_SYSTEM}")

unset(TORX_LIBRARY CACHE) # Re-probe every configure. A cached hit that outlives the file it found fails the build with "torx.h: No such file or directory" and cannot fall back, while a cached NOTFOUND survives into the fetch path and expands to a literal -NOTFOUND include directory.
unset(TORX_INCLUDE_DIR CACHE)

if(TORX_SYSTEM)
	find_library(TORX_LIBRARY NAMES torx)
	find_path(TORX_INCLUDE_DIR NAMES torx.h)
	if(NOT TORX_LIBRARY OR NOT TORX_INCLUDE_DIR)
		message(FATAL_ERROR "TORX_SYSTEM requested but libtorx was not found. Install libtorx, or reconfigure without -D TORX_SYSTEM=ON to fetch and build it.")
	endif()
	message(STATUS "TorX library found; NOT building: ${TORX_LIBRARY}") # TODO maybe verify that it is appropriately shared / static if BUILD_SHARED_LIBS was passed?
	set(TORX_LINK ${TORX_LIBRARY})
else()
	message(STATUS "TorX library NOT used; Building tag ${TORX_TAG}.")
#	set(NO_QR_GENERATOR 1)
	set(NO_AUDIO_CALL 1)
	set(NO_STICKERS 1)
#	set(NO_FILE_TRANSFER 1)
	include(FetchContent)
	if(NOT DEFINED TORX_INSTALL) # A SHARED libtorx is a runtime dependency of the installed binary and must ship with it; a static one is linked in and must not, because leaving it in the prefix is what hijacks the next configure.
		set(TORX_INSTALL ${BUILD_SHARED_LIBS})
	endif()
	FetchContent_Declare(
		torx
		GIT_REPOSITORY ${REPO_TORX}
		GIT_TAG ${TORX_TAG}
		${TORX_FETCH_EXCLUDE}
	)
	FetchContent_MakeAvailable(torx)
	set(TORX_LINK torx)
endif()

add_executable( ${PROJECT_NAME} ncurses.c ${BEEP_H})

find_library(NCURSESW_LIBRARY ncursesw)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED ncursesw) # Must support UTF8
target_include_directories(${PROJECT_NAME} PRIVATE ${NCURSESW_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR} ${TORX_INCLUDE_DIR}) # CMAKE_CURRENT_BINARY_DIR is for BEEP_H

add_compile_definitions(SQLITE_HAS_CODEC SECURE_MALLOC) # necessary, not redundant

if(WIN32)
message(STATUS "TorX Win32")
target_link_libraries(
	${PROJECT_NAME} PRIVATE
		Winmm		## For Wave audio output
		${NCURSESW_LIBRARIES}
		${TORX_LINK}
		z
		sodium
	##	event
		event_core
	##	event_pthreads
		sqlcipher
		Ws2_32		## This MUST be last, or linking errors WILL occur.
)
else()
message(STATUS "TorX Unix")
target_link_libraries(
	${PROJECT_NAME} PRIVATE
		${NCURSESW_LIBRARIES}
		${TORX_LINK}
		z		## TODO required ONLY when BUILD_SHARED_LIBS=0
		sodium		## TODO required ALWAYS for sodium_memzero
	##	event
		event_core	## TODO required ALWAYS for evutil_closesocket (and probably things like evutil_socket_t)
		event_pthreads	## TODO required ONLY when BUILD_SHARED_LIBS=0
		sqlcipher	## TODO required ONLY when BUILD_SHARED_LIBS=0
)
endif()

target_compile_options(${PROJECT_NAME} PRIVATE ${NCURSESW_CFLAGS_OTHER})

# Install the executables
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install the desktop file
install(FILES org.torx.ncurses.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

# Install the icon
install(FILES other/org.torx.ncurses.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps)
