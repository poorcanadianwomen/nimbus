# nimbus

Linux Stoat client implemented in C++20 using Qt6 and CMake.

The interface consists of three primary areas:

1. Account and server navigation
2. Channel and conversation navigation
3. Message transcript

## Signing In

Run:

```
nimbus
```

If no valid session exists, the application displays the authentication form.

Authentication requires:

```
Email address
Password
```

After successful authentication, the session token is stored at:

```
~/.config/nimbus/session
```

The session file is created with permission mode `0600`.

Subsequent launches use the stored session automatically.

The last used email address is stored at:

```
~/.config/nimbus/account
```

The password is not stored.

If the stored session is rejected, the authentication form is displayed with the corresponding failure reason.

Headless authentication:

```
nimbus --login
```

Headless logout:

```
nimbus --logout
```

`$NIMBUS_SESSION_FILE` overrides the default session and account storage location. This variable is used by the test environment and can also be used to create independent profiles.

## Building

### Requirements

CMake 3.24 or newer

C++20 compiler

GCC 12 or newer

Clang 15 or newer

Qt 6.4 or newer

Required Qt modules:

```
Core
Network
Gui
Widgets
Svg
Test
```

`nlohmann/json` is downloaded by CMake during the initial configuration. Network access is therefore required during the first configuration.

Debian and Ubuntu dependency installation:

```
sudo apt install cmake g++ qt6-base-dev qt6-svg-dev
```

`qt6-base-dev` provides the primary Qt modules. `qt6-svg-dev` provides the SVG module.

### Build Procedure

Configure:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
```

Build:

```
cmake --build build -j"$(nproc)"
```

Run tests:

```
ctest --test-dir build --output-on-failure
```

Launch:

```
./build/bin/nimbus
```

Ninja can be selected with:

```
cmake -S . -B build -GNinja -DCMAKE_BUILD_TYPE=Release
```

A clean build on an eight core system requires approximately 70 seconds. Test executables account for most of the build time.

## CMake Options

`NIMBUS_BUILD_TESTS`

Default: `ON`

Builds the seven test suites. Setting this option to `OFF` reduces build time. The application does not require the test executables.

`NIMBUS_WERROR`

Default: `OFF`

Converts compiler warnings into errors.

`CMAKE_BUILD_TYPE`

Default: unset

Supported values include:

```
Release
Debug
RelWithDebInfo
```

For normal application builds, `Release` is recommended.

## nimbus exec

`nimbus exec` is the development build entry point.

```
nimbus exec
nimbus exec build
nimbus exec test
nimbus exec clean
```

The default command performs a clean build and test operation.

`build` performs the build operation.

`test` executes the seven test suites in offscreen mode.

`clean` removes the build trees.

The executor always enables `NIMBUS_BUILD_TESTS`. The generated build therefore corresponds to the tree used for testing.

## Installing the nimbus Command

The launcher is located at:

```
scripts/nimbus-launch.sh
```

Create the command symlink with:

```
ln -s "$PWD/scripts/nimbus-launch.sh" ~/.local/bin/nimbus
```

The launcher builds the application when required and starts the executable.

Authentication state is managed by the application and not by the launcher.

The application can also be started directly:

```
./build/bin/nimbus
```

## Build Diagnostics

Qt6 package configuration failure:

```
Could not find a package configuration file provided by "Qt6"
```

Verify that the required Qt modules are installed, including `qt6-svg-dev`.

Dependency download failure for `nlohmann/json` indicates that the initial CMake configuration does not have network access.

`nimbus` command not found indicates that `~/.local/bin` is not present in `PATH` or that the project directory used by the symlink no longer exists.

Immediate application termination should be investigated from a terminal. With no stored session, the expected behavior is display of the authentication form.

## Command Line Interface

No argument

Launches the client.

`--login`

Reads an email address and password from standard input and stores the resulting session.

`--logout`

Deletes the stored session.

`--watch [seconds]`

Connects to the service, reports state changes, and prints a summary.

`--selftest`

Performs an end to end write operation against the user's Notes channel.

`--verbose`

Enables debug logging.

`--trace`

Enables trace logging.

`--api <url>`

Overrides the API base URL.

Default:

```
https://api.stoat.chat
```

## Implemented Functionality

Authentication with persistent session storage.

Server navigation with account avatar.

Direct channel navigation through the account interface.

Channel grouping by category.

Mention indicators.

Message transcripts.

Author names.

Reply rendering.

Reaction rendering using server emote images.

Link preview cards.

Message creation.

Message editing.

Message deletion.

Message pinning.

File attachments uploaded to the CDN.

Server emote picker.

Single line message composer.

Attachment controls.

Emoji controls.

GIF control.

Send control.

System tray integration.

Tray actions:

```
Show
Quit
```

## Incomplete Functionality

Live message updates are not implemented. `MessageAppend` is currently unhandled.

Message formatting is parsed but is not rendered using the final text layout system.

Supported parsed formatting includes:

```
> quotes
||spoilers||
code
:name: emotes
<@id> mentions
```

Formatting parsing is covered by `tests/test_format.cpp`.

The transcript currently renders message text without the parsed formatting styles.

The GIF control is a placeholder.

MFA authentication is not implemented.

Accounts requiring MFA are rejected with an appropriate status.

Member list functionality is not implemented.

Message search is not implemented.

Dedicated pins view is not implemented.

Sending an empty message through the `+` action produces an attachment request without message body content.

## Tests

The project contains seven test suites with 102 checks.

`nimbus_tests`

Core parsing, storage, sessions, and gateway event frames.

`nimbus_format_tests`

Message syntax parsing including quotes, code, spoilers, emotes, and mentions.

`nimbus_layout_tests`

Transcript geometry and rendered glyph measurements.

`nimbus_shell_tests`

Three pane interface, composer, embeds, and reaction components.

`nimbus_login_tests`

Authentication interface.

`nimbus_icon_tests`

Icon rendering pipeline and pixel validation.

`nimbus_motion_tests`

Reduced motion behavior.

Rendering tests operate on actual generated pixels.

Layout validation measures rendered glyph output and compares the resulting dimensions with expected geometry.

## Architecture

Project structure:

```
src/
    main.cpp
    core/
        app.*
        events.*
        rest.*
        ws.*
        store.*
        models.*
        session.*
    ui/
        chat_window.*
        server_rail.*
        channel_list.*
        message_list.*
        message_delegate.*
        message_format.*
        composer.*
        emoji_picker.*
        avatar_cache.*
        theme.*
```

`main.cpp`

Command line interface and authentication state selection.

`core/app.*`

Owns transport, REST client, cache, and session state.

`core/events.*`

Gateway connection and event frame dispatch.

`core/rest.*`

REST implementation with route specific rate limiting, HTTP 429 retry handling, and multipart uploads.

`core/ws.*`

RFC 6455 WebSocket client over TLS.

`core/store.*`

In memory servers, channels, messages, unread state, and emoji data.

`core/models.*`

Payload parsing and model construction.

`core/session.*`

Persistent credential storage.

`ui/chat_window.*`

Primary three pane interface.

`ui/server_rail.*`

Account and server navigation.

`ui/channel_list.*`

Channel categories and direct messages.

`ui/message_list.*`

Scroll anchored transcript.

`ui/message_delegate.*`

Message rendering including authors, replies, emotes, embeds, and reactions.

`ui/message_format.*`

Message syntax parsing.

`ui/composer.*`

Message input interface.

`ui/emoji_picker.*`

Server emote selection.

`ui/avatar_cache.*`

Authenticated CDN image cache with memory and disk limits.

`ui/theme.*`

Application palette.

`App` owns the transport and application store. UI components bind to `App` and do not directly access lower level transport components.

## Gateway Protocol

Gateway frames are binary.

Incoming frames contain a zlib wrapped deflate stream.

Compression state is maintained per connection rather than per message.

Only the first compressed frame contains the `0x78` header.

Messages terminate with a synchronization flush. `Z_STREAM_END` is therefore not expected at the end of each message.

Outbound messages are not compressed.

Only the inbound direction uses compression.

Authentication is provided as a query parameter in the WebSocket URL.

These behaviors are covered by gateway tests.

## Security

The session token is stored at:

```
~/.config/nimbus/session
```

File permission:

```
0600
```

The session token is a bearer credential. Possession of the token provides account access.

The token is not logged.

The token is not printed by the test suite.

The token is not committed to the repository.

`SessionStore::path()` supports `$NIMBUS_SESSION_FILE`.

The core test suite assigns the session path to a temporary directory to prevent access to the user's real credential.

Tests involving `SessionStore` must set the session path override before performing filesystem operations.

The account address is stored independently at:

```
~/.config/nimbus/account
```

The account file contains the email address used to prefill authentication. It does not contain authentication credentials.
