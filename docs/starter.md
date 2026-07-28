Transmission will be a digital audio workstation.

The first version will act as a host for VST3 plugins. It will be mostly be written in nodejs with a native Linux wrapper to support VST3, both audio and midi. Connections will be made in the form of a graph. The project /home/danny/hyperdata/transmissions already contains a suitable data model and browser-based UI but this should be recreated under /home/danny/github/transmission as a whole new project, with the necessary support for real time processing rather than the event-driven processing of transmissions.

The Steinberg VST SDK is at /chalet/VST_SDK for reference.