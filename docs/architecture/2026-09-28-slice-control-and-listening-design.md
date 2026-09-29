# Slice control and shared listening

Status: six behavior rulings and the bottom-RX chooser area approved by JJ
on September 28, 2026. Full-window flags, applets and multiple-pan behavior
remain under visual review. This document
describes the required behavior; it does not claim that the new commands
or sharing support exist. The Core/GUI implementation belongs in the
current PR; the matching iPhone UI belongs in the phone's dependent PR.

## What the operator should be able to do

From either desktop or phone, see every existing slice, identify its tuning
controller, hear it without disturbing anyone, take control of that same
slice, and leave without stranding radio resources. A slice's letter and
Aether color identify the slice. Separate text identifies who controls it,
whether this device is listening, and whether it is selected for transmit
or actually transmitting.

The current bottom RX banner describes this window's selected receive
slice. The proposed change makes its letter/status an entry to the slice
chooser. Existing mode/filter/DSP badges remain beside it. Selecting an
inventory row inspects it; it does not take control. The phone uses the same
actions and states through its receive-slice UI, with its actual layout to
be reviewed by the phone crew. TX selection remains in the TX controls.

The chooser shows the stable letter/color, frequency, mode/filter,
controller, present/away state, this-device listening state, and TX state.
It distinguishes this window and the desktop running Core from another
device with the same display name. A name is never an authorization key.
An empty window still offers Choose a slice and New slice.

## Approved behavior

| Action or event | Required result |
| --- | --- |
| Listen in | Join the existing slice without allocating another slice or hardware receiver. Its one controller retains tuning. This device controls its own volume and mute. |
| Take control | Transfer the existing slice, preserving letter, color, frequency, mode and filter. The previous controller remains a listener and receives clear notice. |
| Release | This device relinquishes control and stops listening. Other listeners retain audio and the slice becomes available to control. With nobody left, close it and free its resources, including the last physical Core slice. |
| Handoff while TX is selected but idle | Clear that slice's TX selection. Its new controller must select transmit explicitly. Taking RX control grants no transmit permission. |
| Handoff while transmitting | Refuse until transmission stops. Recheck when the action is applied. |
| Missing device | Preserve the existing three-minute reconnect grace. At expiry, remove the absent device's control/listening claims. Keep slices for remaining listeners; close those with nobody left. |

The approved phrases are recorded verbatim in G-118 of the plan addendum.
Any new participant may listen without an extra consent step from the
controller, consistent with JJ's approved paired-device Listen in flow.
Pairing, session admission limits and separate transmit authorization
continue to apply.

## Journeys

Mac controls A, phone joins A: the phone chooses Listen in. Both hear A;
the phone's volume/mute affect only the phone. Its tuning controls explain
that the Mac controls A and offer Take control. Taking control retains A's
tuning and audio, updates both controller labels, and leaves the Mac
listening. The Mac can then leave or take control again.

Selecting another slice on this device changes the active receive focus
and the bottom banner. It does not release previously joined slices or
change the device's transmit selection. Release and Stop listening are
explicit actions. Controller-only edits are disabled for a listener, with
the controller named and Take control reachable.

When the last available slice or hardware receiver is in use, the chooser
still permits Listen in and intact Take control. Those actions use the
existing slice and do not need new capacity. New slice reports the actual
resource constraint and offers existing slices to use. Moving an entire
receiver or closing others to create a different slice remains a distinct
capacity action: the confirmation must name every affected slice and
listener, recheck the current set, and leave everything intact on refusal.

When a device disappears, show it as away during grace. Another device can
take control of an idle slice during grace. A subsequent reconnect restores
only claims the device still has; it cannot steal back transferred control.
Expiry acts on the matching absence generation, so an old timer cannot
release a replacement session's work. Explicit leave releases immediately.

## Full-window proposal under review

The September 28 scout distinguishes window RX focus from each pan's own
selected slice. Existing flag and RX-tab selection updates window RX;
pan-background selection currently updates display focus only. Spectrum
actions target the emitting pan's slice. The shared-listener change must
preserve an explicit target for every control instead of making other pans
silently tune the bottom banner's selection.

The proposed mapping is:

- The bottom chooser inventories all slices. Inspecting a row performs no
  receive or transmit operation. A flag, joined RX tab or explicit Select
  RX focuses that slice, its visible pan, the RX applet and the bottom bar.
- A joined flag retains its Aether letter/color. Text distinguishes You
  control from Listening and names the current controller. A visible slice
  not joined here remains distinguishable as a foreign marker. The same
  details/actions are reachable from the flag and bottom chooser.
- The RX applet shows tabs for this device's joined slices. Under U7 every
  joined slice is visible in one of this device's pans, so there are no
  tabs for hidden joined slices. Listening disables shared tuning and DSP
  edits, with the controller named and Take control reachable. Volume and
  mute stay on the existing AF slider and mute (U5), which Task 14b made
  each listener's own level in the audio mixer; the RX applet has no volume
  or mute (the flag and title bar are the audio surfaces).
- TX remains explicitly bound to the selected transmit slice even when a
  different slice is selected for receive. The current active-RX-dependent
  TX applet bindings need a safety audit when implementing this mapping.
- If a slice is already visible, focus its existing pane, including a
  floating pane (U2: the floater comes forward and the slice becomes this
  window's RX; nothing moves). If unseen, it goes into the main window
  (U1): an empty main-window pane, else the window grows to the next layout
  that fits in the single window, else the operator picks a destination.
  Growing adds no slice to any other empty pane and never opens a floating
  pane. A new view of an existing slice does not
  create a physical receiver or change shared tuning. Replacing or hiding a
  view no longer retains listening: U7 supersedes the earlier proposal that
  listening continue until an explicit leave, and the earlier proposal of
  RX applet tabs for hidden joined slices.

The full-window interactive proposal is
`nereus-multi-pan-slice-flow.html` in this chat's visualization directory.
It was based on actual offscreen Qt two-pan captures recorded in
`core-gui-multi-pan-reference-report.md`. Preview interactions cover
inspection, listening, intact handoff, independent TX, changing RX and
placing/revisiting a hidden slice. It is design evidence, not product
implementation. Placement, floating focus, background-click semantics,
same-pan flag stacking and the exact local-audio presentation still need
JJ's full-flow review. The six approved Core policies remain settled.

## Core boundaries and invariants

Keep tuning authority separate from listening membership. Each live slice
has at most one controller and may have multiple device listeners. The
controller is also a listener. A slice's running identity has an incarnation
distinct from its reusable numeric letter, and its control changes have a
revision. Stale commands and confirmations must not act on a new slice
that later reuses A or on a newer control assignment.

Control/listening changes are serialized on the model thread. Validate the
current authenticated device/session, target incarnation, expected control
revision, current TX gate and capacity effects before mutation. A successful
handoff publishes the new authority without an intermediate destroyed or
unowned slice. Do not rebuild its DSP channel or remove/recreate its audio
source just to change controller. Preserve the existing immutable audio
view and tap lifetime rules across actual slice retirement.

Keep three predicates separate: may see detailed slice state, may receive
its audio, and may change its tuning. Extending an existing ownsSlice check
globally would grant listeners unintended write or TX access. Listeners
need a current read-only mirror of shared tuning and an authoritative
per-device active RX selection; an owner's global active property cannot
represent every listener's independent choice.

Negotiate the new slice-access feature explicitly. Existing clients must
not receive unknown commands or be treated as supporting shared listening.
The hosting desktop must call the same validated Core operations as remote
clients. Its direct RadioModel shortcut must not bypass capacity questions,
TX protection, generation checks or listener cleanup.

Per-listener gain/mute cannot use the shared SliceModel's AF or muted
properties. The existing per-receiver audio tap is before slice mute/pan
and undoes AF gain; verify its zero-gain behavior and stream capacity before
using it for fan-out. Preserve bounded audio resources, clear admission
failures, reconnect/session fencing, and remote audio clock behavior. DSP
callback code must never traverse mutable listener collections or QObject
state. Host speakers and remote playback must obey the same per-device
listening policy without one device muting another.

Zero physical slices is a supported idle Core state. Audit all last-slice
guards and first-slice indexing, active RX/TX fallbacks, saved layout,
display demand, DDC retirement, audio taps, RADE and external diversity.
Closing the last slice must leave valid empty state and a working New slice
path. Preserve tuning preferences without resurrecting an old control claim
or displacing a current user when an absent device returns.

## Verification required before delivery

- Three clients see the same slice identity and current tuning through
  listen, handoff, release and reconnect. Their active RX, volume and mute
  remain independent. The previous controller hears continuous audio after
  handoff and cannot continue writing tuning.
- A listener's forged property writes, tuning verbs and TX attempts are
  refused by Core. Old-session, reused-letter, double-take and stale-confirm
  cases preserve the current authority and all unrelated slices.
- Idle TX selection clears on transfer, on-air transfer refuses, and RX
  control never implicitly keys or grants transmit.
- Last listener leaving closes the slice; remaining listeners prevent its
  closure. Final physical removal reaches a valid zero-slice state and can
  create a new slice again. Device expiry and explicit leave follow the same
  membership rules; a reconnect within grace survives an old expiry event.
- Capacity exhaustion offers useful existing-slice actions. Failed creation,
  pan move and restoration preserve all victims and produce honest notices.
- Hosting desktop, remote desktop and phone have visible pending/refusal
  feedback and equivalent action semantics, including zero-owned states.
  Verify the real desktop layout offscreen before an authorized preview
  launch; the HTML mockup alone is not implementation evidence.

Use the current lane reports and addendum as the work record. G-125's
bounded Add preflight is a separate repair and does not establish general
transactionality for the remaining move/restore paths.
