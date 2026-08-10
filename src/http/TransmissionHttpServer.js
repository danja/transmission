// src/http/TransmissionHttpServer.js
// HTTP REST server wrapping TransmissionControlService.
// POST bodies: text/turtle. GET responses: text/turtle for project data, application/json otherwise.

import { createServer } from 'node:http'
import { ProjectRevisionError } from '../control/TransmissionControlService.js'
import {
  parseChangeSet,
  parseTransportConfigure,
  parseSetParameter,
  parseProjectOpen,
  parseProjectSave,
  parseNewProject,
  ParseError,
  serializeStatus,
  serializeError
} from './TurtleCodec.js'

export class TransmissionHttpServer {
  constructor(control, { port = 7878, bindAddress = '127.0.0.1' } = {}) {
    this.control = control
    this.port = port
    this.bindAddress = bindAddress
    this._server = createServer((req, res) => this._dispatch(req, res))
  }

  listen() {
    return new Promise((resolve, reject) => {
      this._server.once('error', reject)
      this._server.listen(this.port, this.bindAddress, () => resolve(this))
    })
  }

  close() {
    return new Promise((resolve, reject) => {
      this._server.close(error => error ? reject(error) : resolve())
    })
  }

  async _dispatch(req, res) {
    const url = new URL(req.url, `http://${req.headers.host ?? 'localhost'}`)
    const path = url.pathname.replace(/\/$/, '') || '/'
    try {
      if (req.method === 'GET') {
        await this._handleGet(path, req, res)
      } else if (req.method === 'POST') {
        const body = await readBody(req)
        await this._handlePost(path, body, req, res)
      } else {
        sendJson(res, 405, { error: 'Method Not Allowed' })
      }
    } catch (error) {
      if (error instanceof ProjectRevisionError) {
        sendTurtle(res, 409, serializeError(error.message, 'RevisionConflict'))
      } else if (error instanceof ParseError) {
        sendTurtle(res, 422, serializeError(error.message, 'ParseError'))
      } else if (error?.message?.includes('No project')) {
        sendTurtle(res, 404, serializeError(error.message, 'NoProject'))
      } else if (error?.message?.includes('outside the allowed')) {
        sendTurtle(res, 403, serializeError(error.message, 'Forbidden'))
      } else {
        sendTurtle(res, 422, serializeError(error?.message ?? 'Unknown error', 'Error'))
      }
    }
  }

  async _handleGet(path, req, res) {
    const control = this.control

    if (path === '/status') {
      return sendTurtle(res, 200, serializeStatus(control.status()))
    }
    if (path === '/project') {
      return sendJson(res, 200, control.describeProject())
    }
    if (path === '/graph') {
      return sendTurtle(res, 200, control.projectTurtle())
    }
    if (path === '/diagnostics') {
      return sendJson(res, 200, control.diagnostics())
    }
    if (path === '/peaks') {
      return sendJson(res, 200, control.peaks())
    }
    if (path === '/plugins') {
      await control.waitForPluginScan()
      return sendJson(res, 200, control.plugins())
    }
    if (path === '/plugins/profiles') {
      return sendTurtle(res, 200, control.pluginProfilesTurtle())
    }
    if (path === '/plugins/discovered') {
      await control.waitForPluginScan()
      return sendTurtle(res, 200, control.discoveredPluginsTurtle())
    }
    if (path.startsWith('/plugins/')) {
      const id = decodeURIComponent(path.slice('/plugins/'.length))
      return sendJson(res, 200, control.describePlugin(id))
    }

    sendJson(res, 404, { error: 'Not Found' })
  }

  async _handlePost(path, body, req, res) {
    const control = this.control

    if (path === '/graph/changes') {
      const input = await parseChangeSet(body)
      return sendJson(res, 200, control.applyGraphChanges(input))
    }
    if (path === '/transport/play') {
      return sendTurtle(res, 200, serializeStatus(control.startTransport()))
    }
    if (path === '/transport/stop') {
      return sendTurtle(res, 200, serializeStatus(control.stopTransport()))
    }
    if (path === '/transport/configure') {
      const input = await parseTransportConfigure(body)
      const result = control.configureTransport(input)
      return sendTurtle(res, 200, serializeStatus(result))
    }
    if (path.startsWith('/parameters/')) {
      const parts = path.slice('/parameters/'.length).split('/')
      if (parts.length < 2) return sendJson(res, 400, { error: 'Path must be /parameters/:nodeId/:parameterId' })
      const nodeId = decodeURIComponent(parts[0])
      const parameterId = parseInt(decodeURIComponent(parts[1]), 10)
      const input = await parseSetParameter(body)
      const result = control.setParameter({ ...input, nodeId, parameterId })
      return sendJson(res, 200, result)
    }
    if (path === '/projects/new') {
      const definition = await parseNewProject(body)
      return sendJson(res, 200, control.newProject(definition))
    }
    if (path === '/projects/open') {
      const { filePath } = await parseProjectOpen(body)
      return sendJson(res, 200, await control.openProject(filePath))
    }
    if (path === '/projects/save') {
      const { filePath } = await parseProjectSave(body)
      return sendJson(res, 200, await control.saveProject(filePath))
    }
    if (path === '/plugins/scan') {
      const result = await control.scanPlugins()
      return sendJson(res, 202, result)
    }

    sendJson(res, 404, { error: 'Not Found' })
  }
}

function sendJson(res, status, value) {
  const body = JSON.stringify(value, null, 2)
  res.writeHead(status, { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(body) })
  res.end(body)
}

function sendTurtle(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'text/turtle; charset=utf-8', 'Content-Length': Buffer.byteLength(body) })
  res.end(body)
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = []
    req.on('data', chunk => chunks.push(chunk))
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')))
    req.on('error', reject)
  })
}
