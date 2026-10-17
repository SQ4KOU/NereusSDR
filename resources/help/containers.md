# Containers and objects

Containers hold an ordered stack of meter faces and existing applet views. Open
**Containers > Edit Container** or the container's **Container Settings…** menu
to edit its name, appearance, sources and Contents.

## Arrange the workspace

Drag the dotted grip beside an object to reorder it or move it into another
unlocked container. The grip occupies its own gutter; the object's controls keep
their normal operation. The container context menu's **Contents** submenu offers
**Move Up**, **Move Down**, **Move to Container**, **Pop Out** and **Return to
remembered container**. Meter faces can be duplicated. Applet views are singletons:
choose **Move** to relocate their existing view rather than create a second one.

A pop-out shell remembers each object's original container and neighbors. Closing
it returns all objects together. New objects and copies created in the shell
return to the main container. If a remembered container was deleted, the main
container is the fallback. Locked destinations refuse the operation without a
partial move. Closing an ordinary floating container hides it and retains its
placement. **Hide container (retain placement)** also preserves its contents.
Use the Containers menu to show a hidden container again. Removing a container
in Settings returns its contents; removing a meter object explicitly deletes that
entry. **Return / hide view** retains the existing applet.

## Edit, Apply and Cancel

Settings edits a workspace draft. Switching between containers keeps your pending
edits; moving a singleton within the draft takes effect on **Apply**. The preview
uses cached readings and inert controls. It does not tune, transmit or start a
second receiver. **Apply** saves the workspace and updates the live arrangement.
**Cancel** or closing Settings discards unapplied changes.

If another change touches the same container while Settings is open, Apply reports
a conflict. **Reload conflicting containers** refreshes the conflicting containers; unrelated
pending container edits remain. A failed save leaves the live workspace and your
draft intact, so correct the storage problem before applying again.

## Headers, locks and sources

Headers can be **Always visible**, **Reveal on hover / focus**, or **Hidden**.
Header space stays reserved. Hold **Shift** or use **Reveal controls** in the context
menu to recover hidden controls, then change the Header setting. **Lock** prevents
arrangement, floating/docking and resizing; it does not disable the object's normal
radio controls. Auto height follows the stack's actual minimum heights.
**Minimizes** and **Hide when RX unused** are unavailable; their saved values
remain retained for recovery.

A container provides an inherited source, and supported meters may select an
explicit slice/session in Properties. Missing, stale or foreign sources show an
unavailable reading rather than values from a different receiver or station.
Clock and MMIO sources remain independent. An unavailable applet or unsupported
legacy object keeps its place and configuration for later recovery.

## With the workspace look on

This section applies when the workspace look is on, which it is unless the app
was started with `NEREUS_WORKSPACE_PREVIEW=0`. What "Headers, locks and sources"
says about headers and **Lock** describes the app with the look off, where
nothing has changed; with the look on, read this section in its place.

**Headers.** Each container's header is either shown or hidden; there is no
hover choice. The eye button on the header switches it: an open eye means the
header stays, a crossed-out eye means it hides when you are not arranging.
**Keep Header Showing** in the container's right-click menu is the same switch.
A hidden header takes no room, so the content fills the container.

**Arranging.** Hold **Shift** by itself for about a third of a second and every
header appears, hidden ones included, until you let go. Shift with a click, a
wheel turn or another key does not do this, so tuning with Shift held works as
before. To keep arranging without holding a key, choose **Containers > Arrange
Workspace** (Ctrl+Shift+A); **Escape** leaves it. Arranging does not change
which headers are shown or hidden.

**Moving a container.** While arranging, drag a container by its header and
let go where you want it:

* near an edge of the main window, a box shows where it will land, and it
  docks there;
* over the spectrum, it sits free where you dropped it, on an 8 pixel grid
  (hold **Alt** to place it freely);
* outside the main window, it becomes a window of its own.

This works the same for a docked container, a free one and a popped-out window,
so a window dragged back over the main window docks or sits free where you drop
it. **Escape** during a drag puts the container back where it started. When you
are not arranging, dragging a window only moves it, so you can park a window
over the main window without docking it.

**Pop Out, pin and return.** **Pop Out** is in the container's right-click menu
as well as on the header. A popped-out window has a pin on its header: click it
to keep the window on top of other windows, click it again to stop. The return
arrow puts the container back where it was popped out from: the same strip,
beside the same neighbour, or the same free place. A window that was never
docked joins the Main Panel's strip.

**No Lock.** The workspace look has no Lock setting. A container whose header
is hidden has nothing to grab, so it cannot be moved by accident, and a
container saved as locked opens unlocked.

**Where things start.** Every container and window starts where you left it, at
the size you left it, on the monitor you left it on. If that monitor is not
connected the window opens on the main window's screen, and goes back to its
own monitor when that is connected again. This part is the same with the look
off.

## Portable exchange and recovery

**Save / Load** exchange a container; **Export / Import** exchange its entries
through the clipboard. Import stages a draft and validates before Apply. Imported
objects receive new identities; internal return links follow the imported objects.
An imported singleton moves the existing view and preserves its live identity.
Malformed or future structured files are rejected without replacing your draft.

Customized legacy geometry, raw records, unknown fields and MMIO GUID/variable
bindings are retained. Portable recovery records keep the exact original imported
bytes/text through export, Apply and restart, including unsupported objects.
Migration also retains the original legacy settings and creates
`ContainerWorkspaceBackup` on a successful save. Recovery uses the same workspace
codec and store as normal saves; avoid manually rewriting unknown payloads.
