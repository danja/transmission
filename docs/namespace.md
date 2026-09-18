---
layout: default
title: Namespace
nav_order: 8
---

# The `trn:` namespace

`http://purl.org/stuff/transmissions/`, prefix `trn:`.

Every published plugin profile in this project, in
[plugin-universe](https://github.com/danja/plugin-universe), in
[downspout](https://github.com/danja/downspout) and in
[JigDAW](https://github.com/danja/jigdaw) writes IRIs in this namespace. Until 2026-09-18
every one of them resolved to a 404. They resolve now.

## What resolves where

| Request | Answer |
|---|---|
| `http://purl.org/stuff/transmissions/` | 302 to `https://purl.org/stuff/transmissions/`, then to `https://hyperdata.it/xmlns/transmissions/` |
| `https://hyperdata.it/xmlns/transmissions/` | the page, or `transmissions.ttl` when `Accept` asks for RDF |
| `https://hyperdata.it/xmlns/transmissions` | 301 to the form with the trailing slash |
| `https://hyperdata.it/xmlns/transmissions/transmissions.ttl` | the vocabulary, `text/turtle` |
| `https://hyperdata.it/xmlns/transmissions/PluginProfile` | 303 to the namespace IRI |

The PURL redirect already existed: `purl.org/stuff/<name>` maps to `hyperdata.it/xmlns/<name>`
through a wildcard, and JigDAW uses the same one. Nothing at purl.org needed changing. The
only thing missing was anything at the far end.

Two decisions are load bearing and are stated in `deploy/nginx/vocab.conf` beside the lines
that implement them.

**A term answers 303, not 200.** `trn:PluginProfile` denotes a class. Answering 200 would
assert that the class *is* the page returned, and a reasoner that conflates the two starts
inferring that a class is a document.

**Everything carries `Access-Control-Allow-Origin`.** JigDAW's host runs in a browser and
dereferences these IRIs. A response without the header is unreadable rather than merely
untrusted.

## Minting

The namespace ends in a slash and terms are `trn:Foo`, not `trn:vocab#Foo`. It is minted on
the PURL and never on `hyperdata.it`: the PURL is the identity and the host is an
implementation detail. Minting on the serving domain would break every IRI ever published the
day the site moves, including the ones written into other people's files.

## Building and deploying

`vocabs/` holds the terms, split by subject. The served document is all of those files
concatenated, because the namespace is one thing and the sources are several.

```sh
npm run build:vocab     # regenerates deploy/vocab/
npm run check:nginx     # validates deploy/nginx/vocab.conf in a container
npm test                # tests/vocab/site.test.js fails if deploy/vocab is stale
```

`deploy/vocab/` is generated **and committed**, because nginx serves it out of the checkout on
the server and there is no build step there. `tests/vocab/site.test.js` fails if it has drifted
from `vocabs/`, which is the only thing connecting the two.

On the server, once:

```nginx
# inside the existing `server { server_name hyperdata.it; ... }` block,
# beside the JigDAW one
include /home/github/transmission/deploy/nginx/vocab.conf;
```

```sh
cd /home/github/transmission && git pull
sudo nginx -t && sudo systemctl reload nginx
```

To check it:

```sh
curl -sS -H "Accept: text/turtle" https://hyperdata.it/xmlns/transmissions/ | grep -c '^trn:'
curl -sS -o /dev/null -w '%{http_code}\n' https://hyperdata.it/xmlns/transmissions/PluginProfile
```

Expect the term count and 303. Piping the document into `head` makes curl exit 23, which is
`head` closing the pipe on a 42 kB body rather than anything being wrong; `grep -c` and
`sed -n` read to the end and exit 0.

Adding a term afterwards is `npm run build:vocab`, commit, and `git pull` on the server. No
nginx reload: the files are read from disk on every request.

## What is in the document, and what is not

`vocabs/` holds seven files and the served document is all of them:

| File | What |
|---|---|
| `ontology.ttl` | the namespace's own description |
| `profile.ttl` | what a plugin is musically: roles, signal types, routing |
| `project.ttl` | the saved project: graph, nodes, connections, transport, arrangement |
| `parameters.ttl` | the parameter terms downspout's profiles use |
| `formats.ttl` | plugin formats, and the terms retired in favour of `lv2:` |
| `actions.ttl` | the actions an agent can ask a host to perform |
| `djset.ttl` | DJ set lists |

208 terms. Every `trn:` IRI named in `src/rdf/Vocabulary.js` is declared in one of them, and
`tests/vocab/site.test.js` fails if that stops being true. It was not true before: 50 of the
81 terms the code writes into every saved project were declared nowhere at all, and nothing
reported it because nothing connected the two files.

`formats.ttl` came from plugin-universe's `vocabs/trn-extensions.ttl`, whose own header asks
for exactly that: "proposed upstream to `~/github/transmission` rather than maintained
separately". Its copy stays where its SHACL shapes can see it, and the test compares the two
when that checkout is present.

### Instance data in the namespace

Saved projects bind the **default** prefix to this namespace:

```turtle
@prefix : <http://purl.org/stuff/transmissions/> .

:main a :Transmission ; :pipe ( :bassgen :pulse :system-output ) .
```

So every patch node name is minted in the vocabulary namespace. Measured across the four
repositories, 160 such IRIs exist in committed files: `trn:pulse`, `trn:plugin-1`,
`trn:harmonic-atlas`, `trn:plugins/downspout/ambo` and the rest. They are data, not terms, and
the vocabulary document does not define them.

`deploy/nginx/vocab.conf` 303s them to the namespace anyway, which is better than the 404 they
would otherwise get, and is the ordinary behaviour of a slash namespace for an IRI it does not
define. Fixing it properly means giving projects a namespace of their own and rewriting every
committed project file, which is a larger and separate decision: the IRIs are already
published, and changing them changes what every saved project says.
