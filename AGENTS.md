# Nimbus

C++20 / Qt6 Stoat client.

**Use the executor, not raw cmake.** `nimbus-exec` (i.e. `scripts/executor.sh`) is
the build-and-test entry point and is on PATH. `nimbus-exec` = clean + build +
test; `nimbus-exec test` runs the four suites offscreen and returns non-zero on
any failure. It always configures `NIMBUS_BUILD_TESTS=ON` in a single build dir.

Do **not** use the `nimbus` launcher to build. It configures tests OFF on purpose,
so a tree it built cannot be tested. It is for running the app only.

A stale `build-asan/` is a hazard, not just disk usage: it leaves a running
binary that no current source produced, which reads as the app misbehaving.
`nimbus-exec clean` removes both trees.

## Standing visual preferences

The user has asked for these repeatedly. Treat them as settled, not as suggestions
to re-litigate, unless they say otherwise.

- **No borders anywhere.** No frames on inputs, lists, scrollbars, or splitters.
  Selections are fills or opacity, never an outline. The global stylesheet in
  `src/ui/theme.cpp` already sets `border: none` broadly; keep it that way when
  adding widgets.
- **No selected-state fill on the guild rail.** The rail marks the open server by
  dimming the others to 55% opacity instead. Server icons are already full-bleed
  squares, so a pill behind them fights the icon for the same pixels.
- **No presence dots.** Not on DM rows, not on avatars, not in the member list. The
  only alert the client draws is the mention badge on channels where you are
  pinged.
- **One icon set per control, chosen deliberately.** The send control and the home
  glyph are Google Material (`resources/icons/material/`); everything else is
  Tabler (`resources/icons/tabler/`). Do not swap one for the other casually —
  filled control takes a filled mark, background control takes an outline mark,
  because an outline glyph at 16px on a solid fill loses its stroke to
  antialiasing.
- **Compact, dense rhythm.** The look is Ripcord and classic Skype: tight vertical
  spacing, small type, no wasted padding.
- **One theme, Ash.** `ThemePreset` is `{ kAsh, kCustom }` and the Settings picker
  lists only Ash. `kCustom` is not a second palette -- it is how the picker reports
  that the accent has been overridden. Do not add presets back. A config naming a
  retired preset must resolve to Ash via `themePresetFromName`, and the `Theme`
  struct defaults must stay identical to `makeAsh()` because a first run only
  reads the struct.

## Code conventions

- Terse comment style: state what the code does and why the obvious alternative is
  worse. Do not narrate, do not add banner comments, do not comment what a line
  plainly does.
- Prefer a named constant over a magic number when the number is load-bearing.
- Every delegate caches: `sizeHint` and `paint` must not rebuild layout data
  independently.
- Animated state lives in the delegate, keyed by row id, and is seeded to its
  **resting** value on creation. An unstarted tween rests at 0, which silently
  paints the row invisible. See `MessageDelegate::motionFor`.

## Testing

Anything that renders must have an offscreen test that asserts on real pixels, not
just on geometry. A layout that "looks right" in a test that never blits a glyph
proves nothing. `tests/test_composer.cpp` has the pattern: paint a row into a
`QImage`, count pixels that are not the background, and assert it is non-zero.

## Security

The session token lives at `~/.config/nimbus/session`, mode `0600`, written by
`App::login` and read by `App::restoreSession`. It is a bearer credential
equivalent to a password. Never read it into a log, a test, or a commit.

`SessionStore::path()` honours `$NIMBUS_SESSION_FILE`, and the core suite sets it
to a temp directory. That override is load-bearing: the session round-trip test
used to run against the real path, so every `nimbus-exec test` deleted the user's
credential and left a dummy behind. If you add a test that touches `SessionStore`,
it must set the override, and it should assert that the override took effect
before it writes anything.

The last-used email is kept separately at `~/.config/nimbus/account`. It is an
address, not a credential, and exists only to prefill the login form. Do not add
the password, a token, or anything else to it.

`scripts/nimbus-launch.sh` handles no credentials at all. It builds and passes
arguments through; the session is the app's business. If you find a script
reaching into the config directory to read or write one, that is a regression.
