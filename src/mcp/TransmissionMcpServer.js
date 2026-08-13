import { McpServer } from '@modelcontextprotocol/sdk/server/mcp.js'
import * as z from 'zod/v4'

const metadataSchema = z.record(z.string(), z.unknown())
const portsSchema = z.object({
  audioInputs: z.number().int().nonnegative().optional(),
  audioOutputs: z.number().int().nonnegative().optional(),
  midiInputs: z.number().int().nonnegative().optional(),
  midiOutputs: z.number().int().nonnegative().optional()
})
const parameterSchema = z.object({
  id: z.number().int().nonnegative(),
  normalizedValue: z.number().min(0).max(1)
})
const nodeSchema = z.object({
  id: z.string().min(1),
  type: z.string().min(1),
  label: z.string().optional(),
  ports: portsSchema.optional(),
  settings: metadataSchema.optional(),
  parameters: z.array(parameterSchema).optional(),
  state: z.object({ component: z.string().optional(), controller: z.string().optional() }).optional(),
  metadata: metadataSchema.optional()
})
const connectionSchema = z.object({
  from: z.string().min(1),
  to: z.string().min(1),
  kind: z.enum(['audio', 'midi']),
  fromPort: z.number().int().nonnegative().optional(),
  toPort: z.number().int().nonnegative().optional()
})
const graphSchema = z.object({
  id: z.string().min(1),
  label: z.string().optional(),
  nodes: z.array(nodeSchema).default([]),
  connections: z.array(connectionSchema).default([]),
  metadata: metadataSchema.optional(),
  transport: z.object({
    sampleRate: z.number().positive().optional(),
    tempoMap: z.array(z.object({ beat: z.number().nonnegative(), bpm: z.number().positive() })).optional(),
    positionBeats: z.number().nonnegative().optional(),
    loop: z.object({
      startBeat: z.number().nonnegative(),
      endBeat: z.number().positive(),
      enabled: z.boolean().optional()
    }).nullable().optional()
  }).optional()
})
const operationSchema = z.discriminatedUnion('type', [
  z.object({ type: z.literal('addNode'), node: nodeSchema }),
  z.object({
    type: z.literal('updateNode'),
    nodeId: z.string().min(1),
    changes: nodeSchema.partial().omit({ id: true })
  }),
  z.object({ type: z.literal('removeNode'), nodeId: z.string().min(1) }),
  z.object({ type: z.literal('addConnection'), connection: connectionSchema }),
  z.object({ type: z.literal('removeConnection'), connection: connectionSchema }),
  z.object({ type: z.literal('setProjectMetadata'), metadata: metadataSchema })
])

export function createTransmissionMcpServer(control) {
  const server = new McpServer(
    { name: 'transmission', version: '0.1.0' },
    {
      instructions: [
        'Inspect transmission_status or transmission://project before making changes.',
        'Pass the current revision to every mutation.',
        'Use dryRun on graph_apply_changes before applying complex edits.',
        'Structural and transport configuration changes require stopped audio.'
      ].join(' ')
    }
  )

  registerResources(server, control)
  registerTools(server, control)
  return server
}

function registerResources(server, control) {
  server.registerResource(
    'current-project',
    'transmission://project',
    { title: 'Current Transmission project', description: 'Current graph, revision, transport, and runtime status', mimeType: 'application/json' },
    async uri => resource(uri, JSON.stringify(await control.describeProject(), null, 2), 'application/json')
  )
  server.registerResource(
    'current-project-turtle',
    'transmission://project/turtle',
    { title: 'Current project as RDF Turtle', description: 'Canonical persisted representation of the current graph', mimeType: 'text/turtle' },
    async uri => resource(uri, await control.projectTurtle(), 'text/turtle')
  )
  server.registerResource(
    'diagnostics',
    'transmission://diagnostics',
    { title: 'Transmission diagnostics', description: 'Project, transport, engine, and native processing status', mimeType: 'application/json' },
    async uri => resource(uri, JSON.stringify(await control.diagnostics(), null, 2), 'application/json')
  )
  if (control.pluginCatalogue) {
    server.registerResource(
      'plugin-catalogue',
      'transmission://plugins',
      { title: 'Transmission plugin catalogue', description: 'Discovered VST3 facts merged with curated behavioural profiles', mimeType: 'application/json' },
      async uri => {
        await control.waitForPluginScan()
        return resource(uri, JSON.stringify(await control.plugins(), null, 2), 'application/json')
      }
    )
    server.registerResource(
      'plugin-profiles',
      'transmission://plugins/profiles',
      { title: 'Curated plugin profiles as RDF Turtle', description: 'The RDF source of curated musical and behavioural plugin knowledge', mimeType: 'text/turtle' },
      async uri => resource(uri, await control.pluginProfilesTurtle(), 'text/turtle')
    )
    server.registerResource(
      'discovered-plugins',
      'transmission://plugins/discovered',
      { title: 'Discovered VST3 metadata as RDF Turtle', description: 'Technical plugin, topology, and parameter evidence produced by the isolated VST3 scanner', mimeType: 'text/turtle' },
      async uri => {
        await control.waitForPluginScan()
        return resource(uri, await control.discoveredPluginsTurtle(), 'text/turtle')
      }
    )
  }
}

function registerTools(server, control) {
  server.registerTool('transmission_status', {
    title: 'Inspect Transmission status',
    description: 'Get project revision, dirty state, transport, and native engine availability.',
    annotations: readOnly
  }, async () => result(await control.status()))

  server.registerTool('project_get', {
    title: 'Inspect current project',
    description: 'Get the complete current graph and its revision.',
    annotations: readOnly
  }, async () => result(await control.describeProject()))

  server.registerTool('project_new', {
    title: 'Create project',
    description: 'Replace the current project with a new validated graph. This can discard unsaved changes.',
    inputSchema: { project: graphSchema },
    annotations: destructive
  }, async ({ project }) => result(await control.newProject(project)))

  server.registerTool('project_open', {
    title: 'Open project',
    description: 'Open a Turtle or JSON project below an allowed project root. This can discard unsaved changes.',
    inputSchema: { filePath: z.string().min(1) },
    annotations: destructive
  }, async ({ filePath }) => result(await control.openProject(filePath)))

  server.registerTool('project_save', {
    title: 'Save project',
    description: 'Save the current project atomically. A relative path is resolved below the configured project root.',
    inputSchema: { filePath: z.string().min(1).optional() },
    annotations: mutatingIdempotent
  }, async ({ filePath }) => result(await control.saveProject(filePath)))

  server.registerTool('graph_apply_changes', {
    title: 'Apply graph transaction',
    description: 'Validate and atomically apply typed node and connection operations. Use dryRun first for complex edits.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      dryRun: z.boolean().default(false),
      operations: z.array(operationSchema).min(1).max(256)
    },
    annotations: destructive
  }, async input => result(await control.applyGraphChanges(input)))

  server.registerTool('transport_configure', {
    title: 'Configure transport',
    description: 'Configure tempo, loop, or position while audio is stopped.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      tempo: z.coerce.number().positive().optional(),
      atBeat: z.coerce.number().nonnegative().optional(),
      loop: z.object({
        startBeat: z.coerce.number().nonnegative(),
        endBeat: z.coerce.number().positive(),
        enabled: z.boolean().optional()
      }).optional(),
      clearLoop: z.boolean().default(false),
      positionBeats: z.coerce.number().nonnegative().optional()
    },
    annotations: mutatingIdempotent
  }, async input => result(await control.configureTransport(input)))

  server.registerTool('transport_play', {
    title: 'Start audio',
    description: 'Start native audio processing for the loaded project.',
    annotations: mutatingIdempotent
  }, async () => result(await control.startTransport()))

  server.registerTool('transport_stop', {
    title: 'Stop audio',
    description: 'Stop native audio processing.',
    annotations: mutatingIdempotent
  }, async () => result(await control.stopTransport()))

  server.registerTool('parameter_set', {
    title: 'Set plugin parameter',
    description: 'Set and persist a normalized plugin parameter value. Applies live when a native engine is available.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      nodeId: z.string().min(1),
      parameterId: z.coerce.number().int().nonnegative(),
      value: z.coerce.number().min(0).max(1),
      sampleOffset: z.coerce.number().int().nonnegative().default(0)
    },
    annotations: mutatingIdempotent
  }, async input => result(await control.setParameter(input)))

  server.registerTool('transmission_diagnostics', {
    title: 'Inspect engine diagnostics',
    description: 'Read native processing and control-plane diagnostics.',
    annotations: readOnly
  }, async () => result(await control.diagnostics()))

  server.registerTool('peaks_get', {
    title: 'Read output peaks',
    description: 'Read the current left and right output peak levels. Use after transport_play to confirm audio is flowing.',
    annotations: readOnly
  }, async () => result(control.peaks()))

  server.registerTool('node_list', {
    title: 'List graph nodes',
    description: 'Return a compact summary of every node in the current project: id, type, label, and port counts. Lighter than project_get when you only need the node inventory.',
    annotations: readOnly
  }, async () => {
    control.project  // triggers #requireProject indirectly via describeProject — use status check
    const project = await control.describeProject()
    const nodes = project.graph.nodes.map(({ id, type, label, ports }) => ({ id, type, label, ports }))
    return result({ revision: project.revision, nodes })
  })

  server.registerTool('node_add', {
    title: 'Add a plugin node',
    description: 'Add a plugin node to the graph and optionally connect it in one step. Equivalent to a graph_apply_changes addNode + addConnection batch but with a simpler interface. Audio must be stopped.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      node: nodeSchema,
      connections: z.array(connectionSchema).default([])
    },
    annotations: destructive
  }, async ({ expectedRevision, node, connections }) => {
    const operations = [
      { type: 'addNode', node },
      ...connections.map(connection => ({ type: 'addConnection', connection }))
    ]
    return result(await control.applyGraphChanges({ expectedRevision, operations, dryRun: false }))
  })

  server.registerTool('node_remove', {
    title: 'Remove a graph node',
    description: 'Remove a node and all its connections. Audio must be stopped.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      nodeId: z.string().min(1)
    },
    annotations: destructive
  }, async ({ expectedRevision, nodeId }) => {
    return result(await control.applyGraphChanges({
      expectedRevision,
      operations: [{ type: 'removeNode', nodeId }],
      dryRun: false
    }))
  })

  server.registerTool('connection_add', {
    title: 'Add a connection',
    description: 'Add a single audio or MIDI connection between two nodes. Audio must be stopped.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      connection: connectionSchema
    },
    annotations: destructive
  }, async ({ expectedRevision, connection }) => {
    return result(await control.applyGraphChanges({
      expectedRevision,
      operations: [{ type: 'addConnection', connection }],
      dryRun: false
    }))
  })

  server.registerTool('connection_remove', {
    title: 'Remove a connection',
    description: 'Remove a single audio or MIDI connection. Audio must be stopped.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      connection: connectionSchema
    },
    annotations: destructive
  }, async ({ expectedRevision, connection }) => {
    return result(await control.applyGraphChanges({
      expectedRevision,
      operations: [{ type: 'removeConnection', connection }],
      dryRun: false
    }))
  })

  server.registerTool('parameters_set_batch', {
    title: 'Set multiple plugin parameters',
    description: 'Set several normalized parameter values on one node atomically. Applies live when the native engine is running. Use instead of repeated parameter_set calls.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      nodeId: z.string().min(1),
      parameters: z.array(z.object({
        id: z.coerce.number().int().nonnegative(),
        normalizedValue: z.coerce.number().min(0).max(1)
      })).min(1),
      sampleOffset: z.coerce.number().int().nonnegative().default(0)
    },
    annotations: mutatingIdempotent
  }, async input => result(await control.setParameters(input)))

  server.registerTool('arrangement_get', {
    title: 'Get arrangement',
    description: 'Read the current arrangement: length, MIDI clips, and gain automation lanes.',
    annotations: readOnly
  }, async () => result(control.getArrangement()))

  server.registerTool('arrangement_update', {
    title: 'Replace arrangement',
    description: 'Replace the arrangement length, MIDI clips, or gain lanes. Omit any field to leave it unchanged. Use clip_add / clip_remove for targeted edits.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      lengthBeats: z.coerce.number().nonnegative().optional(),
      midiClips: z.array(z.object({
        id: z.string().min(1),
        targetNodeId: z.string().min(1),
        startBeat: z.coerce.number().nonnegative(),
        lengthBeats: z.coerce.number().positive(),
        notes: z.array(z.object({
          startBeat: z.coerce.number().nonnegative(),
          durationBeats: z.coerce.number().positive(),
          pitch: z.coerce.number().int().min(0).max(127),
          velocity: z.coerce.number().int().min(1).max(127),
          channel: z.coerce.number().int().min(0).max(15).default(0)
        })).default([])
      })).optional(),
      gainLanes: z.array(z.object({
        targetNodeId: z.string().min(1),
        parameterId: z.coerce.number().int().nonnegative(),
        points: z.array(z.object({
          beat: z.coerce.number().nonnegative(),
          value: z.coerce.number().min(0).max(1)
        })).default([])
      })).optional()
    },
    annotations: mutatingIdempotent
  }, async input => result(await control.updateArrangement(input)))

  server.registerTool('clip_add', {
    title: 'Add a MIDI clip',
    description: 'Add a single MIDI clip with notes to the arrangement. The clip id must be unique.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      clip: z.object({
        id: z.string().min(1),
        targetNodeId: z.string().min(1),
        startBeat: z.coerce.number().nonnegative(),
        lengthBeats: z.coerce.number().positive(),
        notes: z.array(z.object({
          startBeat: z.coerce.number().nonnegative(),
          durationBeats: z.coerce.number().positive(),
          pitch: z.coerce.number().int().min(0).max(127),
          velocity: z.coerce.number().int().min(1).max(127),
          channel: z.coerce.number().int().min(0).max(15).default(0)
        })).default([])
      })
    },
    annotations: mutatingIdempotent
  }, async input => result(await control.addArrangementClip(input)))

  server.registerTool('clip_remove', {
    title: 'Remove a MIDI clip',
    description: 'Remove a MIDI clip from the arrangement by id.',
    inputSchema: {
      expectedRevision: z.coerce.number().int().nonnegative(),
      clipId: z.string().min(1)
    },
    annotations: mutatingIdempotent
  }, async input => result(await control.removeArrangementClip(input)))

  server.registerTool('project_capture_midi', {
    title: 'Capture project MIDI to file',
    description: 'Run the native engine for a given number of beats, intercept MIDI output from every node, and write a single multi-track SMF. One track per source node, named after the node label. Requires the native engine (--native-addon). Audio must be stopped before calling.',
    inputSchema: {
      filePath: z.string().min(1),
      durationBeats: z.coerce.number().positive().default(64)
    },
    annotations: mutatingIdempotent
  }, async ({ filePath, durationBeats }) => result(await control.captureProjectMidi({ filePath, durationBeats })))

  server.registerTool('arrangement_render_midi', {
    title: 'Render arrangement to MIDI file',
    description: 'Write the arrangement MIDI clips to a Standard MIDI File (Type 1). Produces one tempo track and one track per target instrument node. A relative path is resolved below the configured project root.',
    inputSchema: { filePath: z.string().min(1) },
    annotations: mutatingIdempotent
  }, async ({ filePath }) => result(await control.renderMidi(filePath)))

  if (control.pluginCatalogue) {
    server.registerTool('plugins_list', {
      title: 'List plugins',
      description: 'List curated and discovered plugins, optionally limiting results to installed VST3 bundles.',
      inputSchema: { installedOnly: z.boolean().default(false) },
      annotations: readOnly
    }, async input => {
      await control.waitForPluginScan()
      return result(await control.plugins(input))
    })

    server.registerTool('plugins_search', {
      title: 'Search plugin capabilities',
      description: 'Search plugins by text, semantic roles, produced or accepted signal types, vendor, and installation state.',
      inputSchema: {
        query: z.string().default(''),
        roles: z.array(z.string()).default([]),
        produces: z.array(z.string()).default([]),
        accepts: z.array(z.string()).default([]),
        vendor: z.string().default(''),
        installedOnly: z.boolean().default(false)
      },
      annotations: readOnly
    }, async input => {
      await control.waitForPluginScan()
      return result(await control.searchPlugins(input))
    })

    server.registerTool('plugin_describe', {
      title: 'Describe plugin',
      description: 'Get merged discovered facts, behavioural knowledge, recommendations, requirements, and cautions for one plugin.',
      inputSchema: { identifier: z.string().min(1) },
      annotations: readOnly
    }, async ({ identifier }) => {
      await control.waitForPluginScan()
      return result(await control.describePlugin(identifier))
    })

    server.registerTool('plugin_validate_chain', {
      title: 'Validate plugin chain',
      description: 'Check adjacent plugins for compatible semantic MIDI or audio signal flow and return curated notes.',
      inputSchema: { identifiers: z.array(z.string().min(1)).min(2).max(64) },
      annotations: readOnly
    }, async ({ identifiers }) => {
      await control.waitForPluginScan()
      return result(await control.validatePluginChain(identifiers))
    })

    server.registerTool('plugins_scan', {
      title: 'Rescan VST3 plugins',
      description: 'Run each configured VST3 bundle through the isolated metadata inspector and refresh discovered facts.',
      annotations: {
        readOnlyHint: false,
        destructiveHint: false,
        idempotentHint: true,
        openWorldHint: true
      }
    }, async () => result(await control.scanPlugins()))
  }
}

const readOnly = {
  readOnlyHint: true,
  destructiveHint: false,
  idempotentHint: true,
  openWorldHint: false
}
const mutatingIdempotent = {
  readOnlyHint: false,
  destructiveHint: false,
  idempotentHint: true,
  openWorldHint: false
}
const destructive = {
  readOnlyHint: false,
  destructiveHint: true,
  idempotentHint: false,
  openWorldHint: false
}

function result(value) {
  return {
    content: [{ type: 'text', text: JSON.stringify(value, null, 2) }],
    structuredContent: value
  }
}

function resource(uri, text, mimeType) {
  return { contents: [{ uri: uri.href, text, mimeType }] }
}
