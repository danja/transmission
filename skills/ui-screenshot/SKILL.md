---
name: ui-screenshot
description: Capture and inspect the native GTK UI on the host desktop using its X11 session.
---

# UI Screenshot Capture

Use this workflow when implementing or reviewing Transmission’s native GTK UI.

1. Build the optional UI target:

   ```sh
   cmake -S native -B native/build-ui -DTRANSMISSION_WITH_GTK_UI=ON
   cmake --build native/build-ui --target transmission_graph_ui
   ```

2. Launch it against the host desktop X11 session. Preserve the terminal session so the process stays alive:

   ```sh
   DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority native/build-ui/transmission_graph_ui
   ```

3. Locate the window with `wmctrl -lG`, then capture the individual window rather than the whole desktop:

   ```sh
   DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority \
     import -window <window-id> /tmp/transmission-ui.png
   ```

4. Inspect `/tmp/transmission-ui.png` with the image viewer tool. Repeat after visual changes. The desktop may have multiple monitors; window geometry from `wmctrl -lG` identifies which display contains the UI.

The GTK settings file may emit a warning if malformed; this does not prevent the graph window from rendering. Do not put screenshots in the repository unless they are intentionally requested as project assets.
