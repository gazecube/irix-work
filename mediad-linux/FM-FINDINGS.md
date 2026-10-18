# IRIX media and desktop integration findings

The installed file manager is /usr/bin/fm (also /usr/sbin/fm), with an existing IRIX compatibility runtime. It was launched with `fm -b` for this test. No drive icon integration has been demonstrated.

The new native mediad prototype and the IRIX device namespace are isolated in sibling output directories. mediad builds and publishes drive inventory over a private Unix socket using its original media XDR serialization. The installed fm has not been connected to that socket. The fm trace contains FAM portmapper lookups (391002), which are not mediad lookups (391017).

Both optical drives are discovered. Read access to /dev/sr0 and /dev/sr1 fails with EACCES for the current account. Unknown media state was corrected to avoid reporting a disc as present on probe failure. The earlier live-media-snapshot.json predates that correction and must not be used as proof of disc presence.

The source docs eoe/cmd/mediad/doc/design.html:12-16 and doc/monitor_api.html:20-27 explicitly describe removal of objectserver and its replacement with the mediad monitor client API, implemented by eoe/lib/libmediaclient/src/mediaclient.c. A legacy FileManager resource mentioning objectserver does not establish a current dependency.

DefaultDeviceList explicitly excludes removable media. genDeviceIcons/bgicons handle other requested device types, and should not be mistaken for the removable-media desktop path.

Remaining work: verify the installed fm removable-media client path; provide its discovery/transport compatibility; bridge the shared namespace and inventory into the existing runtime; test optical access, filesystem recognition, events, desktop objects and open/eject behavior. Raw aliases currently resolve to Linux block devices and do not implement IRIX raw-character-device semantics or IRIX ioctls. Slice mappings are explicit, not guessed from Linux partition numbers.
