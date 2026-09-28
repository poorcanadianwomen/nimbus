# nimbus

A [Stoat](https://stoat.chat) client for Linux, in C++20 with Qt6 and CMake.

Three panes: the account and your servers on the left, the channel or conversation
list in the middle, the transcript on the right. Dark, compact, and built to stay
out of the way.

## Signing in

```sh
nimbus
```

With no stored session the app opens a sign-in form — an email address, a password,
and a button. On success the session token is written to `~/.config/nimbus/session`
with mode `0600`, and every launch after that goes straight to the client. There is
no token to paste and no account menu to find.

The last-used address is kept in `~/.config/nimbus/account` so the form can prefill
it. The password is never stored anywhere.

If the stored session is later rejected, the client returns to the form with the
reason rather than opening an empty window.

Headless equivalents:

```sh
nimbus --login     # prompts on stdin, saves the session
nimbus --logout    # deletes the stored session
```

`$NIMBUS_SESSION_FILE` relocates both `session` and `account`. That is how the test
suite avoids touching real credentials, and how you would run a second profile.

## Building

### Requirements

| | Version | Notes |
| --- | --- | --- |
| CMake | 3.24 or newer | 3.24 is what `CMakeLists.txt` asks for |
| A C++20 compiler | GCC 12+, Clang 15+ | `CMAKE_CXX_STANDARD 20` is required, not optional |
| Qt | **6.4 or newer** | `Core`, `Network`, `Gui`, `Widgets`, `Svg`, and `Test` for the suites |

nlohmann/json is fetched by CMake at configure time, so **the first configure needs
network access**. Nothing else is vendored.

On Debian and Ubuntu:

```sh
sudo apt install cmake g++ qt6-base-dev qt6-svg-dev
```

Note the two Qt packages: the base module and the SVG module are separate. If CMake
stops at `Could not find a package configuration file provided by "Qt6"`, the Svg
module is the one you are missing.

### The four commands

```sh
# 1. configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# 2. build
cmake --build build -j"$(nproc)"

# 3. test -- optional, and off by default in a plain configure
ctest --test-dir build --output-on-failure

# 4. run
./build/bin/nimbus
```

A clean build takes about **70 seconds** on eight cores, most of it the shell
suites, which compile the UI several times over because each suite is its own
executable with its own `main`.

Add `-GNinja` to the configure line if you prefer it; there is nothing in the
project that depends on Make.

### Options

| Option | Default | What it does |
| --- | --- | --- |
| `NIMBUS_BUILD_TESTS` | `ON` | Build the seven test suites. Turn it `OFF` to cut build time roughly in half — the app itself does not need them |
| `NIMBUS_WERROR` | `OFF` | Treat warnings as errors. The project builds warning-free; turn this on in CI |
| `CMAKE_BUILD_TYPE` | unset | `Release`, `Debug`, or `RelWithDebInfo`. Unset means no optimisation flags at all, which is not what you want for a build you intend to run |

### Or use the executor

`nimbus-exec` is the supported entry point for development and wraps the same
commands:

```sh
nimbus-exec            # clean, build, test
nimbus-exec build      # build only
nimbus-exec test       # the seven suites, offscreen
nimbus-exec clean      # remove both build trees
```

It always configures `NIMBUS_BUILD_TESTS=ON`, because the tree it builds is the
tree it tests. A launcher that configured tests off could produce a build you could
not test, which is the failure mode the executor exists to prevent.

### Installing the `nimbus` command

Optional. `scripts/nimbus-launch.sh` is the `nimbus` command, and it is a symlink
rather than a copy, so it keeps working when the script changes:

```sh
ln -s "$PWD/scripts/nimbus-launch.sh" ~/.local/bin/nimbus
```

It builds on demand and then runs the app, and it handles no credentials — the
session is the app's business. Without it, run `./build/bin/nimbus` directly.

### When a build goes wrong

**`Could not find a package configuration file provided by "Qt6"`** — a Qt module is
missing. Check the five above; `qt6-svg-dev` is the one people miss.

**Configure tries to download nlohmann/json and fails** — the first configure needs
network. Once `_deps/` is populated, later configures are offline.

**`nimbus` command not found** — the symlink is not in a directory on `PATH`, or the
build tree moved. `~/.local/bin` is on `PATH` on most Debian systems.

**The app opens and immediately exits** — run it from a terminal to see why. With no
stored session it opens the sign-in form and waits; that is expected, not a crash.

## Command line

| Flag | What it does |
| --- | --- |
| *(none)* | Launch the client |
| `--login` | Prompt for email and password, save the session |
| `--logout` | Delete the stored session |
| `--watch [seconds]` | Connect, report state changes, and print a summary |
| `--selftest` | End-to-end write check against your own Notes channel |
| `--verbose` | Debug-level logging |
| `--trace` | Trace-level logging |
| `--api <url>` | Override the API base (default `https://api.stoat.chat`) |

## What works

- **Sign in**, with the session remembered across launches.
- **Servers** in the rail, headed by your own avatar. Direct channels are not in the
  rail; they are behind the avatar, which lists them newest first.
- **Channels** grouped by category, with mention badges.
- **Transcripts** with author names, replies quoting the message they answer,
  reactions rendering the server's actual emote images, and link previews as cards.
- **Sending**, editing, deleting, pinning, and file attachments uploaded to the CDN.
- **An emoji picker** over the server's emotes, resolved to real images by id.
- **A single-line composer** capped and centred to line up with the transcript's text
  column, with attach, emoji, GIF and send controls.
- **A tray icon** that raises the client, with Show and Quit.

## Not done yet

Honest list, so you know what you are looking at:

- **Live messages are dropped.** `MessageAppend` is unhandled, so a channel you have
  open does not update when someone posts. This is the most visible gap.
- **Message formatting is parsed but not yet rendered.** `> ` quotes, `||spoiler||`,
  code, `:name:` emotes and `<@id>` mentions are parsed into spans and tested
  (`tests/test_format.cpp`); painting them needs a real text layout in the delegate.
  Until that lands the transcript draws message text plainly.
- **The GIF control is a placeholder** with a tooltip that says so.
- **MFA is not implemented.** An account that requires it is told so.
- **No member list, search, or pins view.**
- **Empty messages are sent as attachments.** Triggering a send from `+` has no body.

## Tests

Seven suites, 102 checks, all offscreen:

| Suite | What it covers |
| --- | --- |
| `nimbus_tests` | Core: parsing, the store, sessions, event frames |
| `nimbus_format_tests` | Message syntax: quotes, code, spoilers, emotes, mentions |
| `nimbus_layout_tests` | Transcript geometry against real painted glyphs |
| `nimbus_shell_tests` | The three panes, the composer, embeds, reaction pills |
| `nimbus_login_tests` | The sign-in form |
| `nimbus_icon_tests` | The icon pipeline, by counting pixels |
| `nimbus_motion_tests` | Reduce-motion behaviour |

Anything that renders is asserted on real pixels. A layout test that never blits a
glyph proves nothing, so the delegate suites count non-background pixels and compare
measured heights against painted ones.

## Architecture

```
src/
  main.cpp            CLI, and the window the auth state selects
  core/
    app.*             Owns the transport, the REST client, the cache and the session
    events.*          The gateway connection and frame dispatch
    rest.*            REST with per-route buckets, 429 retries, multipart upload
    ws.*              RFC 6455 client over TLS
    store.*           In-memory servers, channels, messages, unreads, emoji
    models.*          Payload to model parsing
    session.*         The stored credential
  ui/
    chat_window.*     The three panes, and what opens when
    server_rail.*     The account and the servers
    channel_list.*    Channels by category, or the direct messages
    message_list.*    Scroll-anchored transcript
    message_delegate.*  One message row: names, replies, emotes, embeds, reactions
    message_format.*  Message syntax into spans
    composer.*        The message input
    emoji_picker.*    The server's emotes
    avatar_cache.*    Authenticated CDN images, capped in memory and on disk
    theme.*           One palette
```

`App` owns the transport and the store. The UI binds to `App` and nothing below it.

### The gateway

Details that are easy to get wrong, and are covered by tests:

- Frames are **binary** and carry a **zlib-wrapped** deflate stream.
- The stream is **per connection, not per message**: only the first frame carries the
  `0x78` header.
- Each message ends with a **sync flush**, so `Z_STREAM_END` never arrives and a
  decoder that waits for it discards every frame.
- **Outbound messages are left uncompressed.** Only the inbound direction is
  compressed; sending a wrapped stream earned an immediate decoding error.
- Authentication is a **query parameter** on the socket URL, not a frame.

## Security

The session token lives at `~/.config/nimbus/session`, mode `0600`. It is a bearer
credential: anyone holding it acts as the account. It is never logged, never printed
by the tests, and never committed.

`SessionStore::path()` honours `$NIMBUS_SESSION_FILE`, and the core suite sets it to
a temporary directory. That override is load-bearing: the session round-trip test
used to run against the real path, so every test run deleted the credential. If you
add a test that touches `SessionStore`, set the override and assert it took effect
before writing anything.

The last-used email is kept separately at `~/.config/nimbus/account`. It is an
address, not a credential, and exists only to prefill the form.
