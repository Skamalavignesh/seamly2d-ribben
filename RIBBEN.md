# Ribben Addon

Ribben is a fork-local addition to Seamly2D: an embedded, opt-in live
JSON-RPC server compiled straight into `seamly2d.exe`, so an MCP client
(Claude) can read and edit the *currently open* pattern instead of only the
`.sm2d` file on disk. It's the missing piece the sibling
[`seamly2d MCP`](../seamly2d%20MCP) project's `PROJECT_PLAN.md` flagged: no
live scripting API, only file-and-CLI integration. This project fixes that
by giving Seamly2D itself an addon, the same way `freecad-mcp`'s
[`addon/FreeCADMCP`](../freecad-mcp/addon/FreeCADMCP) gives FreeCAD one.

## Architecture

```
Claude / MCP client
      │  JSON-RPC 2.0, newline-delimited, over TCP
      ▼
RibbenServer (src/libs/ribben)        <- embedded in seamly2d.exe
      │
      ├─ RibbenIpFilter   loopback + allow-list check per connection
      ├─ token check      shared secret from RibbenSettings
      └─ RibbenDispatcher routes {"method": "..."} to RibbenHost
                                            │
                                            ▼
                                MainWindow (implements RibbenHost)
                                  src/app/seamly2d/core/ribbenmainwindowhost.cpp
                                            │
                                            ▼
                                  doc (VPattern) / pattern (VContainer)
                                  -- the same live objects the GUI edits --
```

`src/libs/ribben` has **no dependency on the app** (MainWindow, VPattern,
etc.) — it only knows about the abstract `RibbenHost` interface
(`ribbenhost.h`). `MainWindow` implements that interface in
`src/app/seamly2d/core/ribbenmainwindowhost.cpp`, which is the only file
that touches both the RPC layer and Seamly2D's real pattern-editing
classes. That split means the RPC/JSON plumbing could be reused for
`seamlyme.exe` (or unit-tested) without pulling in the whole GUI app.

`RibbenServer` runs entirely on the GUI thread via Qt's own event loop —
there's no worker thread, so every `RibbenHost` method executes exactly
like a menu action's slot would. That's simpler than `freecad-mcp`'s addon,
which needs a `dispatch_to_gui` queue because Python's threaded
`SimpleXMLRPCServer` runs handlers off the GUI thread.

## Enabling it

Menu: **Utilities → Enable Ribben Addon (Live MCP Server)**. Off by
default; the on/off state persists across restarts (`RibbenSettings`,
stored in the same ini file as the rest of Seamly2D's settings, under the
`Ribben/` group).

Default: `127.0.0.1:51230`, allow-list `127.0.0.1,::1`. **Bound to loopback
only in this version** — not configurable from the UI yet (see TODO). Every
request must include the token `RibbenSettings::token()` generates on first
use.

## Protocol

One JSON object per line, in both directions.

Request:
```json
{"id": 1, "method": "ping", "token": "<token>"}
```

Success:
```json
{"jsonrpc": "2.0", "id": 1, "result": {"ok": true, "app": "seamly2d", "protocol": 1}}
```

Error:
```json
{"jsonrpc": "2.0", "id": 1, "error": {"code": 3, "message": "No increment named \"foo\"."}}
```

### Methods (v1)

| Method | Params | Mirrors (file-based MCP tool) |
| --- | --- | --- |
| `ping` | — | `ping` |
| `get_status` | — | (new: live file path / modified / unit / piece count) |
| `read_pattern` | — | `read_pattern` |
| `list_increments` | — | `list_increments` |
| `update_increment` | `name`, `formula` | `update_increment` (also live-recomputes geometry) |
| `set_pattern_notes` | `text` | `set_pattern_notes` |
| `list_points` | `draft_block_name` | `list_points` |
| `add_point_single` | `draft_block_name`, `name`, `x`, `y` | `add_point_single` |
| `add_point_end_line` | `draft_block_name`, `name`, `base_point`, `length`, `angle`, `line_type`? | `add_point_end_line` |
| `add_point_along_line` | `draft_block_name`, `name`, `first_point`, `second_point`, `length` | `add_point_along_line` |
| `add_line` | `draft_block_name`, `first_point`, `second_point`, `line_type`? | `add_line` |
| `add_spline` | `draft_block_name`, `first_point`, `second_point`, `angle1`?, `length1`?, `angle2`?, `length2`? | `add_spline` |
| `add_arc` | `draft_block_name`, `center_point`, `radius`, `angle1`, `angle2` | `add_arc` |
| `list_pieces` | `draft_block_name` | `list_pieces` |
| `add_piece` | `draft_block_name`, `name`, `outline`, `seam_allowance`?, `seam_allowance_width`? | `add_piece` |

The geometry methods are the live counterpart to the sibling `seamly2d MCP`
project's file-based `xml_geometry.py` tools, and are kept in sync with
them deliberately (same point/curve types, same attribute names). Unlike
every method above them, these mutate the actually-open pattern's draft
geometry, not just its parametric/metadata layer, and the new geometry
appears on screen **immediately** -- via the same
`doc->Parse(Document::FullParse)` full-scene-rebuild-from-XML that
Seamly2D's own Undo/Redo already triggers live on every use
(`MainWindow::fullParseFile()`), just triggered here by a fresh insertion
into the live DOM instead of an undo step. See `ribbenmainwindowhost.cpp`'s
`ribbenReparseOrRollback` for the rollback behavior if the insertion turns
out to be invalid (e.g. a length/angle formula that doesn't evaluate) --
the bad element is removed and the document re-parsed again before the
error is reported, so a failed call doesn't leave the live pattern
half-broken. `add_piece` is the one exception that can insert several
elements in one call (a `<modeling>` wrapper for every referenced point/
curve, plus the `<piece>` itself); its own inline rollback in
`MainWindow::addPiece` covers all of them together, not just one, so a
failed piece never leaves orphaned modeling wrappers behind. `add_piece`
mirrors a wrinkle Seamly2D's own internal representation has that isn't
obvious from the schema alone: a `<piece>` can't reference `<calculation>`
geometry directly -- every referenced point/spline/arc first needs a thin
wrapper in the draft block's `<modeling>` section (fresh id, `idObject`
pointing back at the original), which `add_piece` creates automatically.

Error codes follow JSON-RPC 2.0 reserved ranges (`-32600`..`-32603`) plus
`RibbenError`'s own: `1` unauthorized, `2` no pattern open, `3` not found,
`4` invalid formula (see `src/libs/ribben/ribbenhost.h`).

## Build environment

`Seamly2D.pro` hard-fails qmake if `$$PWD`/`$$OUT_PWD` contains a space, so
this checkout was moved from its original `E:\new begin\seamly2D ribben`
location to the space-free `E:\seamly2d-ribben` (a directory *junction* at
the old path doesn't work around this -- qmake canonicalizes it back to the
real, spaced target path). Built and verified with a from-scratch Qt 6.7.3 +
MinGW 11.2.0 toolchain (installed via `aqtinstall`, no Qt account needed --
see `aqt install-qt`/`aqt install-tool` in shell history) plus a from-source
`xerces-c` build, since the vendored MinGW `xerces-c` binary in this repo
turned out to be an import stub with no matching runtime DLL (upstream's own
CI only ever builds Windows via MSVC). Three pre-existing upstream Qt 6.7+
compatibility bugs (unrelated to this addon, in `vobj`/`vdxf`/`vpatterndb`)
had to be fixed to get a clean build -- see git log.

## TODO / next steps

- [x] Build it for real and fix whatever surfaces -- done; see "Build
      environment" above.
- [x] Point the sibling [`seamly2d MCP`](../seamly2d%20MCP) Python server at
      this live server -- done (`ribben_client.py` + the `live_*` tools).
- [x] Live geometry creation (`add_point_single`/`add_point_end_line`/
      `add_point_along_line`/`add_line`/`list_points`) -- done, verified
      live against a real running instance (new points appear in the open
      window immediately, via the same full-rebuild-from-XML pass Undo/Redo
      already uses).
- [ ] Settings UI (Preferences page) for port/allowed-ips instead of only
      the checkbox + hardcoded defaults.
- [ ] Surface the token to the user (status bar / dialog) so they can paste
      it into the MCP client's config — right now it's only in the ini file.
- [ ] More live tools: `read_measurements`/`update_measurements` against
      the currently-loaded measurement file, `render_pattern` against the
      live in-memory pattern (currently only the file-based MCP does this).
- [x] More live geometry types: curves/arcs, and piece outlines -- done
      (`add_spline`/`add_arc`/`list_pieces`/`add_piece`), verified live
      against a real running instance including the rollback-on-error path.
- [ ] Decide whether to widen beyond loopback (would need the allow-list to
      actually matter, plus a real security pass on the token exchange).
