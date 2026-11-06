# lcnc-flatserver protocol

Status: draft, shape only. Every table here will grow fields; none of the
rules below is expected to change.

## Files

| File | What it declares |
|---|---|
| `lcfb.fbs` | The two envelopes: `ClientMessage` over `ClientPayload`, `ServerMessage` over `ServerPayload`. Generate from this one. |
| `ids.ts` / `ids.h` | The two file identifiers, hand-written, copied beside the generated code. |
| `common.fbs` | Enums shared by several files, in LinuxCNC's own numbering where one exists. |
| `session.fbs` | `Hello` / `Welcome`: schema version, the poll rate, what the machine is (axes, joints, spindles), what the server offers. |
| `status.fbs` | The `Status` frame: a preamble and ten sections, pushed at the server's poll rate. |
| `command.fbs` | Machine commands, one per NML message the server sends, and their `Ack`. |
| `hal.fbs` | The HAL component: inventory, ordered writes, reads, watched pins. |
| `event.fbs` | Things the server reports unasked: operator errors, input devices, connection. |
| `preview.fbs` | Program preview: the request, the result, and the layout of the bulk file. |
| `tool.fbs` | Tool table rows. |
| `ini.fbs` | INI queries, resolved by the server so path rules have one home. |
| `fs.fbs` | Files on the machine's host: list a directory, read, write, delete, make and unmake a directory — confined to the roots the machine declared. |

## Framing

One stream — a Unix socket, or TCP where the server was given `--tcp`, which
is a debugging door and refused off loopback unless it was explicitly asked
for — both directions, frames of
`uint32 little-endian length` followed by a FlatBuffer. A frame from the
client has root `ClientMessage` and file identifier `LCNC`; a frame from the
server has root `ServerMessage` and file identifier `LCNS`. A receiver
decodes exactly one root, so neither side can be handed a frame meant for
the other. Nothing else is on the wire.

## Generating

`./generate.sh [outdir]` — one `flatc` run, one tree, used by both sides.
The default outdir is **`../src/protocol`**, which is committed: the server
builds there without `flatc` installed, and a TS client can import the
generated code straight from the repository. The tree is replaced wholesale
on each run, so a table removed from the schema does not leave its file
behind.

Because flatc stamps its own version into the generated C++, that tracked
copy compiles only against a matching libflatbuffers. Generate it with the
flatc of the machine that will build without one. Where flatc *is*
installed the build ignores the tracked tree entirely and generates its own,
so the constraint binds nobody else.
The schema declares **no `root_type`**: flatc can carry one root per
generated tree, and each side builds one envelope and parses the other.
Every table gets `getRootAs…` / `GetRoot<>` regardless, and the identifier
is an argument to `finish`, so the roots cost nothing to declare by hand —
`ids.ts` / `ids.h` hold the two identifiers and the calling convention.

## Rules

- **One connection, in order.** The server applies frames in the order
  received on one connection, on one thread. That is the only ordering
  guarantee, and it is what makes `HalWrite`'s ordered list meaningful.
- **A reply says *received*, not *done*.** `Ack.result == Received` means
  task took the message (`EMC_WAIT_RECEIVED`). Completion is observed on
  `Status`. The server never blocks a reply on the machine.
- **Refusals are named.** `Ack.result == Refused` carries `reason`, a
  sentence for a person. A client never has to guess why.
- **`Status` is whole unless the client asks otherwise.** By default every
  frame carries every field and `Status.full` is true; the client diffs, or
  does not bother. A client that sets `Hello.deltas` gets the same frame
  with the *sections* whose readings have not moved omitted, and gets no
  frame at all on a tick where nothing moved — with a heartbeat frame at
  `ServerInfo.heartbeat_ms` so a still machine and a stalled server stay
  distinguishable.

- **`Status.changed` names what moved, and `Status.known` what this server
  answers at all** — one bit per section, both drawn on the `StatusSection`
  bit-mask enum so the two answers about a section compose (`changed &
  Axes`, `known & Hal`) and the identity of a section is declared once. The
  server diffs the *readings*, never the encoded bytes — two builds of equal
  data are not equal buffers — and it writes the mask and decides which
  sections to include from the **one** comparison, so what `changed` says
  and what is present cannot come to disagree.

  On a delta frame `changed` says exactly what presence says. It is worth
  having anyway: it is one field to test instead of ten null checks, and it
  is empty on a heartbeat. **On a whole frame it is the only thing that says
  it** — every section is there every time, so presence carries no
  information — which is what lets a client take whole frames, needing no
  merge and no accumulated state, and still repaint only what moved. On the
  first frame of a connection there is no frame before it, so `changed`
  equals `known`.

  It is section-granular by decision: the sections are grouped by what moves
  together, so a bit per *field* would be sixty-odd bits restating what ten
  already say, maintained by hand against every field ever added.

  So an absent section has exactly one meaning: **outside `known` is "never
  answered", inside `known` is "unchanged since the frame before"**, and
  `full` means neither applies.

- **The poll rate is asserted, not requested.** The server runs one tick
  for every client, so `Hello.poll_ms` is a claim about it: the first
  client to state a non-zero value sets it, and a later client stating a
  *different* one is refused by name rather than quietly served at some
  other rate. Zero — the default — accepts whatever is running, which is
  what a client with no opinion sends. The floor is `Poll.MinMs`, 10 ms.
  When the client that set the rate goes away the assertion lifts and the
  server returns to its command line.

- **Watching a HAL pin is opt-in and opt-out, over `HalWatch`.** `Hello`
  carries no watch list: what a client wants to see changes as the operator
  moves between screens, and a set fixed at connect would have to be the
  union of every screen's. The message **replaces** the whole set, so it is
  idempotent and the two sides cannot drift about what is watched; an empty
  list watches nothing. `HalWatchAck` names what was resolved, in the order
  `Status.watched` will carry it, and what was not, with a reason each.
- **Errors are queued, never coalesced.** Every `OperatorMessage` the error
  channel produced is delivered, in order. This is why a server shedding a
  backlog sheds only `Status`: a status frame is superseded by the next one
  and a message is not.
- **A client that stops reading is resynchronised, not disconnected.** The
  server sheds the `Status` frames it has not begun sending, and a client
  reading deltas is then sent a `full` frame — a gap in a delta sequence can
  only be closed by a frame that stands alone. `Status.seq` jumps by however
  many were shed, so **a gap in `seq` arrives with `full` set**, and that
  pairing is the whole of what a client has to know about falling behind.
- **Unbounded bulk never crosses the stream.** A preview result names a
  file; the stream carries the name and the summary. `fs.fbs` does carry
  bytes, and the difference is that its bytes are bounded: a program is a
  file a person wrote, `ServerInfo.max_file_bytes` says how large one may
  be, and a read past it is truncated rather than streamed. Geometry has no
  such bound and is still a file the client is told the name of.
- **The file frames are confined, and confined by default.**
  `ServerInfo.file_roots` names the directories `fs.fbs` answers for; a path
  outside every one of them, resolved, is refused before it is opened, over
  the Unix socket and over `--tcp` alike. The default root is the **machine
  ini's own directory** and needs no declaration; a `[FILES]ROOT` line
  replaces it rather than adding to it. `DirRead` with an empty path answers
  the roots themselves — or, where there is exactly one, its contents, told
  apart by `DirReadValue.path`. Roots are fixed for the life of the server,
  so reading `file_roots` once at connect stays correct. What the server
  enforces is containment; what an *operator* may see and where they may
  write is the client's rule, and neither is a copy of the other.
- **Schema mismatch is refused by name.** `Welcome.refused` says which
  version the server speaks. `Schema.Version` in `common.fbs` bumps on any
  incompatible change; adding a field is compatible (FlatBuffers rule).
- **A dropped connection is `StopAll`** unless the client's `Hello` said
  `stop_on_disconnect = false`. The server aborts motion and stops every jog
  when a client that asked for it goes away; the default asks for it.
- **Vocabulary is the server's, not LinuxCNC's headers.** Names here follow
  the client-facing model, not `emc_nml.hh`. LinuxCNC enum *values* are
  kept where a number crosses the boundary unchanged.

