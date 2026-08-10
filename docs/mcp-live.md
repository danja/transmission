# MCP Live — Design and Implementation Plan

This document tracks the plan for connecting the Transmission MCP interface directly to the live running GTK application. It will serve as documentation once implemented.

---

## Problem

The MCP server (`scripts/transmission-mcp.js`) runs over stdio as an independent process. It instantiates its own `TransmissionControlService` and manages its own project state. The GTK application (`native_graph_ui_main.cpp`) maintains separate state, communicating with Node.js only via ad-hoc subprocess invocations of `native-ui-project.js`. These two processes have no awareness of each other: changes made through MCP do not appear in the GTK window, and vice versa.

---

## Proposed Solution

Embed an HTTP server in the Transmission control plane so that the GTK app and MCP share a single authoritative `TransmissionControlService`. The server accepts POST requests with RDF Turtle bodies and responds in Turtle. GET endpoints serve status and read-only queries. MCP tools become thin HTTP clients to this server rather than direct callers of the control service.

```
┌────────────────────────────────────────────────┐
│  transmission-live.js (Node.js process)         │
│  ┌──────────────────────────────────────────┐  │
│  │  TransmissionControlService              │  │
│  │  (ProjectSession, EngineSession,         │  │
│  │   PluginCatalogue, NativeBridge)         │  │
│  └────────────┬─────────────────────────────┘  │
│               │                                  │
│  ┌────────────▼─────────────────────────────┐  │
│  │  TransmissionHttpServer (Express)        │  │
│  │  POST /graph/changes   (Turtle body)     │  │
│  │  POST /transport/play                    │  │
│  │  POST /transport/stop                    │  │
│  │  POST /transport/configure (Turtle body) │  │
│  │  POST /parameters/:node/:param           │  │
│  │  POST /projects/new    (Turtle body)     │  │
│  │  POST /projects/open   (Turtle body)     │  │
│  │  POST /projects/save   (Turtle body)     │  │
│  │  GET  /status                            │  │
│  │  GET  /graph                             │  │
│  │  GET  /plugins                           │  │
│  │  GET  /plugins/:id                       │  │
│  │  GET  /diagnostics                       │  │
│  └──────────────────────────────────────────┘  │
└────────────┬───────────────────────────────────┘
             │  localhost:7878
    ┌────────┴────────────────────┐
    │                             │
┌───▼─────────┐        ┌─────────▼──────────┐
│  GTK app    │        │  MCP server        │
│  (C++ GTK3) │        │  TransmissionMcp   │
│  speaks HTTP│        │  Server (HTTP mode)│
│  to control │        │  calls HTTP API    │
│  plane      │        │  instead of direct │
└─────────────┘        └────────────────────┘
```

---

## HTTP API

Content type for all requests and responses: `text/turtle`.

### Read endpoints (GET)

| Endpoint | Description |
|---|---|
| `GET /status` | Project revision, dirty flag, transport state, engine state |
| `GET /graph` | Full graph in Turtle |
| `GET /plugins` | VST3 catalogue (installed and profiles) |
| `GET /plugins/:id` | Single plugin with merged discovered + curated knowledge |
| `GET /diagnostics` | Engine real-time stats |

### Mutation endpoints (POST)

All mutations require an `trn:expectedRevision` in the request body to prevent lost updates. Response is `204 No Content` on success, or `409 Conflict` on revision mismatch, or `422 Unprocessable Entity` on validation failure with a Turtle error body.

| Endpoint | Body | Description |
|---|---|---|
| `POST /graph/changes` | `trn:ChangeSet` with `trn:operations` list | Apply node/connection operations |
| `POST /transport/play` | empty or `trn:PlayAction` | Start audio engine |
| `POST /transport/stop` | empty or `trn:StopAction` | Stop audio engine |
| `POST /transport/configure` | `trn:ConfigureTransport` with tempo/loop/position | Set transport parameters |
| `POST /parameters/:node/:param` | `trn:SetParameter` with `trn:normalizedValue` | Live parameter set |
| `POST /projects/new` | Full graph Turtle | Replace project |
| `POST /projects/open` | `trn:OpenProject` with `trn:filePath` | Load from disk |
| `POST /projects/save` | `trn:SaveProject` with `trn:filePath` | Save to disk |
| `POST /plugins/scan` | empty | Rescan VST3 bundles (async, returns `202`) |

---

## Vocabularies

### `vocabs/profile.ttl`

Extracts and formalises the plugin profile terms currently defined across `src/rdf/Vocabulary.js` and `profiles/downspout.ttl`:

- `trn:PluginProfile`, `trn:DiscoveredPlugin`
- Role classes: `trn:MidiGenerator`, `trn:AudioEffect`, `trn:AudioMixer`, `trn:AudioInstrument`, `trn:AudioSource`, `trn:AudioSink`
- Signal type instances: `trn:Audio`, `trn:Midi`, `trn:BassMidi`, `trn:DrumMidi`, `trn:MelodyMidi`
- Profile properties: `trn:role`, `trn:produces`, `trn:accepts`, `trn:requires`, `trn:recommendedBefore`, `trn:recommendedAfter`, `trn:companion`, `trn:genre`, `trn:caution`, `trn:homepage`
- Discovery properties: `trn:bundleName`, `trn:vstClassId`, `trn:vendor`, `trn:category`, `trn:parameter`

### `vocabs/actions.ttl`

New vocabulary for HTTP API actions:

```turtle
@prefix trn: <http://purl.org/stuff/transmissions/> .
@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .
@prefix owl:  <http://www.w3.org/2002/07/owl#> .
@prefix xsd:  <http://www.w3.org/2001/XMLSchema#> .

## Action hierarchy
trn:Action          a owl:Class .
trn:GraphAction     a owl:Class ; rdfs:subClassOf trn:Action .
trn:TransportAction a owl:Class ; rdfs:subClassOf trn:Action .
trn:ProjectAction   a owl:Class ; rdfs:subClassOf trn:Action .
trn:PluginAction    a owl:Class ; rdfs:subClassOf trn:Action .

## Graph operations
trn:ChangeSet       a owl:Class ; rdfs:subClassOf trn:GraphAction .
trn:AddNode         a owl:Class ; rdfs:subClassOf trn:GraphAction .
trn:UpdateNode      a owl:Class ; rdfs:subClassOf trn:GraphAction .
trn:RemoveNode      a owl:Class ; rdfs:subClassOf trn:GraphAction .
trn:AddConnection   a owl:Class ; rdfs:subClassOf trn:GraphAction .
trn:RemoveConnection a owl:Class ; rdfs:subClassOf trn:GraphAction .
trn:SetProjectMetadata a owl:Class ; rdfs:subClassOf trn:GraphAction .

## Transport operations
trn:PlayAction          a owl:Class ; rdfs:subClassOf trn:TransportAction .
trn:StopAction          a owl:Class ; rdfs:subClassOf trn:TransportAction .
trn:ConfigureTransport  a owl:Class ; rdfs:subClassOf trn:TransportAction .

## Parameter operation
trn:SetParameter a owl:Class ; rdfs:subClassOf trn:Action .

## Project operations
trn:OpenProject a owl:Class ; rdfs:subClassOf trn:ProjectAction .
trn:SaveProject a owl:Class ; rdfs:subClassOf trn:ProjectAction .
trn:NewProject  a owl:Class ; rdfs:subClassOf trn:ProjectAction .

## Plugin operations
trn:ScanPlugins a owl:Class ; rdfs:subClassOf trn:PluginAction .

## Common properties
trn:operations         a owl:ObjectProperty .   # rdf:List of operation blank nodes
trn:expectedRevision   a owl:DatatypeProperty ; rdfs:range xsd:integer .
trn:dryRun             a owl:DatatypeProperty ; rdfs:range xsd:boolean .
trn:filePath           a owl:DatatypeProperty ; rdfs:range xsd:string .
trn:nodeId             a owl:DatatypeProperty ; rdfs:range xsd:string .
trn:parameterId        a owl:DatatypeProperty ; rdfs:range xsd:string .
trn:normalizedValue    a owl:DatatypeProperty ; rdfs:range xsd:decimal .
```

---

## Implementation Steps

### Step 1 — Vocabulary files

- Create `vocabs/actions.ttl` (full file, from spec above).
- Create `vocabs/profile.ttl` by extracting terms from `src/rdf/Vocabulary.js` and `profiles/downspout.ttl` into a standalone OWL/RDFS declaration.

### Step 2 — HTTP server module

Create `src/http/TransmissionHttpServer.js`:

- Wraps an existing `TransmissionControlService` instance.
- Uses Node.js built-in `http` (no Express dependency) or `express` if already present.
- Parses `text/turtle` request bodies with `@rdfjs/parser-n3`.
- Serialises responses to Turtle using `src/rdf/TransmissionRdf.js`.
- Handles content negotiation (`Accept: text/turtle` or `application/ld+json`).
- Exposes all endpoints listed in the API table above.
- Emits SSE (`GET /events`) for real-time revision/transport notifications (optional, phase 2).

Helper: `src/http/TurtleCodec.js` — parse Turtle body → action object; serialise result → Turtle string.

### Step 3 — Configuration file

Create `config.ttl` at the repository root as the startup configuration:

```turtle
@prefix trn: <http://purl.org/stuff/transmissions/> .
@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .

[] a trn:ServerConfig ;
    trn:port 7878 ;
    trn:bindAddress "127.0.0.1" ;
    trn:allowedRoots ( "." ) .
```

- `trn:ServerConfig`, `trn:port`, `trn:bindAddress` terms go in `vocabs/actions.ttl`.
- `transmission-live.js` loads `config.ttl` on startup; command-line flags override individual values.
- `config.ttl` is gitignored to allow per-machine overrides (a `config.defaults.ttl` is committed instead).

### Step 4 — Live server entry point

Create `scripts/transmission-live.js`:

- Replaces the ad-hoc `native-ui-project.js` subprocess model.
- Loads `config.ttl` (from `--config` flag or `./config.ttl`); falls back to `config.defaults.ttl`.
- Initialises `TransmissionControlService` with engine, allowed roots, plugin roots from config and argv.
- Binds `TransmissionHttpServer` to `127.0.0.1` (localhost only) on the configured port.
- Keeps process alive; handles `SIGTERM`/`SIGINT` for clean engine shutdown.
- Logs startup message: `Transmission live server ready on http://localhost:7878`.

### Step 5 — GTK app integration

Modify `native/src/native_graph_ui_main.cpp` and `native/CMakeLists.txt`:

- Add `libcurl` as a `find_package` dependency in `CMakeLists.txt` (`FindCURL`, link `CURL::libcurl`).
- On startup, spawn `transmission-live.js` as a persistent child process (replacing ad-hoc `native-ui-project.js` subprocess invocations).
- Wait for the `ready` log line on the child's stdout before enabling the UI.
- Replace all `native-ui-project.js load/save/serialize` subprocess calls with `libcurl` HTTP calls to `localhost:7878` on a background thread.
- On graph edit (add node, connect, drag, parameter), POST a `trn:ChangeSet` to `/graph/changes`.
- On open/save menu actions, POST to `/projects/open` or `/projects/save`.
- Poll `GET /status` every 500 ms to refresh transport display and detect external changes (e.g., from MCP).
- On shutdown, `kill(child_pid, SIGTERM)` and `waitpid`.

The background HTTP thread must not block the GTK main thread. Use a dedicated `std::thread` posting results back via `g_idle_add`. Keep `native-ui-project.js` working so the GTK app degrades gracefully when the live server is unavailable.

### Step 6 — MCP server HTTP mode

Modify `src/mcp/TransmissionMcpServer.js`:

- Add a constructor option `httpEndpoint` (default: `null`).
- When `httpEndpoint` is set, replace direct `TransmissionControlService` calls with HTTP requests to the live server.
- Create `src/http/TransmissionHttpClient.js`: thin client that mirrors the `TransmissionControlService` method signatures, translating to/from HTTP+Turtle.
- Update `scripts/transmission-mcp.js` to accept `--live` flag, which sets `httpEndpoint` to `http://localhost:7878`.

This preserves the existing stdio MCP mode for offline use and adds live connectivity when the GTK app is running.

### Step 7 — Tests

- `test/http/TransmissionHttpServer.test.js`: unit tests for each endpoint using a mock `TransmissionControlService`.
- `test/http/TurtleCodec.test.js`: round-trip parse/serialise tests for each action type.
- `test/http/TransmissionHttpClient.test.js`: integration test against a real server started in-process.

---

## File Checklist

| File | Status |
|---|---|
| `vocabs/actions.ttl` | done |
| `vocabs/profile.ttl` | done |
| `config.defaults.ttl` | done |
| `config.ttl` | gitignored, generated on first run |
| `src/http/TransmissionHttpServer.js` | done |
| `src/http/TurtleCodec.js` | done |
| `src/http/TransmissionHttpClient.js` | done |
| `scripts/transmission-live.js` | done |
| `native/CMakeLists.txt` | done (libcurl via find_package) |
| `native/src/native_graph_ui_main.cpp` | done (poll, sync-after-save, [live] title) |
| `scripts/transmission-mcp.js` | done (--live flag) |
| `tests/http/TurtleCodec.test.js` | done |
| `tests/http/TransmissionHttpServer.test.js` | done |

---

## Resolved Decisions

1. **Port configuration** — `config.ttl` loaded at startup; defaults committed as `config.defaults.ttl`; port 7878.
2. **Authentication** — localhost-only (`127.0.0.1` bind) for now; bearer token auth deferred until network exposure is needed.
3. **SSE vs polling** — GTK UI polls `/status` at 500 ms for now; SSE deferred to a later phase.
4. **libcurl** — add as a CMake `find_package` dependency; link `CURL::libcurl` in the GTK UI target.
5. **Backward compatibility** — `native-ui-project.js` remains functional; GTK app degrades gracefully when live server is absent.
