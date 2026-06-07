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

## START BLOCK: Try to keep this the same in torx-gtk4 and libtorx. XXX It WILL be passed to libtorx, but libtorx also has a default in case of independent build. XXX
	#### C90 crap: -Wvla -Wdeclaration-after-statement
	#### Unimportant: -Waggregate-return
	#### Heavily CPU + RAM + time intensive junk static analyzer that throws mainly "possible null dereference" warnings (requires 9+gb ram): -fanalyzer
	#### other static analyzer binaries: cppcheck clang-tidy
if(NOT DEFINED CMAKE_C_FLAGS OR CMAKE_C_FLAGS STREQUAL "")
	if (CMAKE_BUILD_TYPE STREQUAL "Debug") ## XXX Dev builds can add either of: -fsanitize=thread -fsanitize=address
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=0 -O0")
		if(NOT WIN32)
			set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined -fsanitize=float-cast-overflow -fsanitize=float-divide-by-zero")
		endif()
	else()
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_FORTIFY_SOURCE=2 -O2")
	endif()
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fwrapv -mshstk -Wstrict-overflow=5 -Wshadow -Wpedantic -Wcast-qual -Wno-overlength-strings -Wwrite-strings -Wnull-dereference -Wstrict-prototypes -Wstrict-aliasing -Wmissing-prototypes -Wmissing-declarations -fstack-protector-strong -Wconversion -Wformat-security")
	string(FIND ${CMAKE_C_COMPILER} "clang" found_clang)
	if(found_clang GREATER -1)
		set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wcast-align -Weverything -Wno-missing-variable-declarations -Wno-declaration-after-statement -Wno-cast-function-type-strict -Wno-reserved-identifier -Wno-padded -Wno-vla -Wno-used-but-marked-unused -Wno-disabled-macro-expansion -Wno-documentation -Wno-documentation-unknown-command -Wno-double-promotion -Wno-unsafe-buffer-usage -Wno-pre-c11-compat -Wno-switch-enum -Wno-covered-switch-default")
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

find_library(torx NAMES torx PATHS ${CMAKE_INSTALL_LIBDIR})
find_path(TORX_INCLUDE_DIR NAMES torx.h PATHS ${CMAKE_INSTALL_INCLUDEDIR})

if(TORX_INCLUDE_DIR AND torx)
	message(STATUS "TorX library found; NOT building.") # TODO maybe verify that it is appropriately shared / static if BUILD_SHARED_LIBS was passed?
else()
	message(STATUS "TorX library NOT found; Building.")
#	set(NO_QR_GENERATOR 1) # Need this for write_bytes
	set(NO_AUDIO_CALL 1)
	set(NO_STICKERS 1)
#	set(NO_FILE_TRANSFER 1)
	include(FetchContent)
	FetchContent_Declare(
		torx
		GIT_REPOSITORY ${REPO_TORX}
		GIT_TAG ${TORX_TAG}
	)
	FetchContent_MakeAvailable(torx)
endif()

add_executable( ${PROJECT_NAME} ncurses.c ${BEEP_H})

find_library(NCURSESW_LIBRARY ncursesw)
find_package(PkgConfig REQUIRED)
pkg_check_modules(NCURSESW REQUIRED ncursesw) # Must support UTF8
target_include_directories(${PROJECT_NAME} PRIVATE ${NCURSESW_INCLUDE_DIRS} ${CMAKE_CURRENT_BINARY_DIR}) # CMAKE_CURRENT_BINARY_DIR is for BEEP_H

add_compile_definitions(SQLITE_HAS_CODEC SECURE_MALLOC) # necessary, not redundant

if(WIN32)
message(STATUS "TorX Win32")
target_link_libraries(
	${PROJECT_NAME} PRIVATE
		Winmm		## For Wave audio output
		${NCURSESW_LIBRARIES}
		torx
		png
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
		torx
		png		## TODO required ONLY when BUILD_SHARED_LIBS=0
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
