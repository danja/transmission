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
    async uri => resource(uri, JSON.stringify(control.describeProject(), null, 2), 'application/json')
  )
  server.registerResource(
    'current-project-turtle',
    'transmission://project/turtle',
    { title: 'Current project as RDF Turtle', description: 'Canonical persisted representation of the current graph', mimeType: 'text/turtle' },
    async uri => resource(uri, control.projectTurtle(), 'text/turtle')
  )
  server.registerResource(
    'diagnostics',
    'transmission://diagnostics',
    { title: 'Transmission diagnostics', description: 'Project, transport, engine, and native processing status', mimeType: 'application/json' },
    async uri => resource(uri, JSON.stringify(control.diagnostics(), null, 2), 'application/json')
  )
  if (control.pluginCatalogue) {
    server.registerResource(
      'plugin-catalogue',
      'transmission://plugins',
      { title: 'Transmission plugin catalogue', description: 'Discovered VST3 facts merged with curated behavioural profiles', mimeType: 'application/json' },
      async uri => {
        await control.waitForPluginScan()
        return resource(uri, JSON.stringify(control.plugins(), null, 2), 'application/json')
      }
    )
    server.registerResource(
      'plugin-profiles',
      'transmission://plugins/profiles',
      { title: 'Curated plugin profiles as RDF Turtle', description: 'The RDF source of curated musical and behavioural plugin knowledge', mimeType: 'text/turtle' },
      async uri => resource(uri, control.pluginProfilesTurtle(), 'text/turtle')
    )
    server.registerResource(
      'discovered-plugins',
      'transmission://plugins/discovered',
      { title: 'Discovered VST3 metadata as RDF Turtle', description: 'Technical plugin, topology, and parameter evidence produced by the isolated VST3 scanner', mimeType: 'text/turtle' },
      async uri => {
        await control.waitForPluginScan()
        return resource(uri, control.discoveredPluginsTurtle(), 'text/turtle')
      }
    )
  }
}

function registerTools(server, control) {
  server.registerTool('transmission_status', {
    title: 'Inspect Transmission status',
    description: 'Get project revision, dirty state, transport, and native engine availability.',
    annotations: readOnly
  }, async () => result(control.status()))

  server.registerTool('project_get', {
    title: 'Inspect current project',
    description: 'Get the complete current graph and its revision.',
    annotations: readOnly
  }, async () => result(control.describeProject()))

  server.registerTool('project_new', {
    title: 'Create project',
    description: 'Replace the current project with a new validated graph. This can discard unsaved changes.',
    inputSchema: { project: graphSchema },
    annotations: destructive
  }, async ({ project }) => result(control.newProject(project)))

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
      expectedRevision: z.number().int().nonnegative(),
      dryRun: z.boolean().default(false),
      operations: z.array(operationSchema).min(1).max(256)
    },
    annotations: destructive
  }, async input => result(control.applyGraphChanges(input)))

  server.registerTool('transport_configure', {
    title: 'Configure transport',
    description: 'Configure tempo, loop, or position while audio is stopped.',
    inputSchema: {
      expectedRevision: z.number().int().nonnegative(),
      tempo: z.number().positive().optional(),
      atBeat: z.number().nonnegative().optional(),
      loop: z.object({
        startBeat: z.number().nonnegative(),
        endBeat: z.number().positive(),
        enabled: z.boolean().optional()
      }).optional(),
      clearLoop: z.boolean().default(false),
      positionBeats: z.number().nonnegative().optional()
    },
    annotations: mutatingIdempotent
  }, async input => result(control.configureTransport(input)))

  server.registerTool('transport_play', {
    title: 'Start audio',
    description: 'Start native audio processing for the loaded project.',
    annotations: mutatingIdempotent
  }, async () => result(control.startTransport()))

  server.registerTool('transport_stop', {
    title: 'Stop audio',
    description: 'Stop native audio processing.',
    annotations: mutatingIdempotent
  }, async () => result(control.stopTransport()))

  server.registerTool('parameter_set', {
    title: 'Set plugin parameter',
    description: 'Set and persist a normalized plugin parameter value. Applies live when a native engine is available.',
    inputSchema: {
      expectedRevision: z.number().int().nonnegative(),
      nodeId: z.string().min(1),
      parameterId: z.number().int().nonnegative(),
      value: z.number().min(0).max(1),
      sampleOffset: z.number().int().nonnegative().default(0)
    },
    annotations: mutatingIdempotent
  }, async input => result(control.setParameter(input)))

  server.registerTool('transmission_diagnostics', {
    title: 'Inspect engine diagnostics',
    description: 'Read native processing and control-plane diagnostics.',
    annotations: readOnly
  }, async () => result(control.diagnostics()))

  if (control.pluginCatalogue) {
    server.registerTool('plugins_list', {
      title: 'List plugins',
      description: 'List curated and discovered plugins, optionally limiting results to installed VST3 bundles.',
      inputSchema: { installedOnly: z.boolean().default(false) },
      annotations: readOnly
    }, async input => {
      await control.waitForPluginScan()
      return result(control.plugins(input))
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
      return result(control.searchPlugins(input))
    })

    server.registerTool('plugin_describe', {
      title: 'Describe plugin',
      description: 'Get merged discovered facts, behavioural knowledge, recommendations, requirements, and cautions for one plugin.',
      inputSchema: { identifier: z.string().min(1) },
      annotations: readOnly
    }, async ({ identifier }) => {
      await control.waitForPluginScan()
      return result(control.describePlugin(identifier))
    })

    server.registerTool('plugin_validate_chain', {
      title: 'Validate plugin chain',
      description: 'Check adjacent plugins for compatible semantic MIDI or audio signal flow and return curated notes.',
      inputSchema: { identifiers: z.array(z.string().min(1)).min(2).max(64) },
      annotations: readOnly
    }, async ({ identifiers }) => {
      await control.waitForPluginScan()
      return result(control.validatePluginChain(identifiers))
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
