<img alt="Logo" width="200" height="200" src="https://raw.githubusercontent.com/TorX-Chat/torx-gtk4/main/other/scalable/apps/logo-torx-symbolic.svg" align="right" style="position: relative; top: 0; left: 0;">

### TorX Ncurses Client (torx-ncurses)
This page is primarily for developers and contributors.
<br>If you are simply looking to download and run TorX, go to [Download](https://torx.chat/#download)
<br>If you want to contribute, see [Contribute](https://torx.chat/#contribute) and our [TODO Lists](https://torx.chat/todo.html)

#### Build Instructions:
##### Linux:

###### Install build dependencies:
`sudo apt install git cmake libsodium-dev libevent-dev libsqlcipher-dev build-essential libncurses-dev`

###### Install runtime dependencies:
`sudo apt install tor snowflake-client obfs4proxy`

###### Clone the repository
`git clone https://github.com/TorX-Chat/torx-ncurses && cd torx-ncurses`
nic
###### For building TorX normally:
`cmake -D TORX_TAG=main -B build && cd build && make && cd ..  && ./build/torx-ncurses`

###### For installing TorX (after building):
`cd build && sudo make install`

###### For uninstalling TorX (after installing):
`sudo xargs rm < install_manifest.txt`

##### OSX:
See Linux instructions, then modify as appropriate. CMakeLists.txt may need modifications. When successful, contact us so that we can add instructions.

##### Windows:
<br><a href="https://www.msys2.org/">Install MSYS2</a> then open a terminal by clicking "MSYS2 MINGW64"
```
pacman -Syu && exit
pacman -S git mingw-w64-x86_64-gcc mingw-w64-x86_64-libsodium mingw-w64-x86_64-libevent mingw-w64-x86_64-sqlcipher mingw-w64-x86_64-cmake mingw-w64-x86_64-toolchain base-devel
git clone https://github.com/TorX-Chat/torx-ncurses && cd torx-ncurses
cmake -G "Unix Makefiles" -D TORX_TAG=main -B build/ && cd build && make clean && make
GSK_RENDERER=cairo build/torx-ncurses.exe
```

#### Voluntary Contribution Licensing Agreement:
Subject to implicit consent: Ownership of all ideas, suggestions, issues, pull requests, contributions of any kind, etc, are non-exclusively gifted to the original TorX developer without condition nor consideration, for the purpose of improving the software, for the benefit of all users, current and future. Any contributor who chooses not to apply this licensing agreement may make an opt-out statement when making their contribution.
Note: The purpose of this statement is so that TorX can one day be re-licensed as GPLv2, GPLv4, AGPL, MIT, BSD, CC0, etc, in the future, if necessary. If you opt-out, your contributions will need to be stripped if we one day need to re-license and we're unable to contact you for your explicit consent. You may opt-out, but please don't.

#### Screenshots:
TODO

