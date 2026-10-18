# Qt compatibility references

| NereusSDR file | Qt source/version | Kind and modification |
| --- | --- | --- |
| src/gui/QtCocoaAccessibilityOwnershipGuard.mm | qtbase v6.11.0 src/plugins/platforms/cocoa/qcocoaaccessibilityelement.mm:90–105,219–226,257–268,312–340 | App-local wrapper: preserve parent-owned synthetic IDs while forwarding original Qt method bodies. Exact Qt source header retained. Objective-C runtime metadata and plugin UUID were verified against the crashing installed arm64 binary. No Qt method body is copied. JJ Boyd (KG4VCF), 2026-10-03, with OpenAI Codex assistance. |

This uses private Qt class/ivar ABI inside the GUI process. It is limited to
the proven Qt6.11.0 Cocoa image, requires explicit startup refusal if that
targeted image/ABI has drifted or another interceptor owns the methods, and
must be removed when the Qt dependency carries the upstream ownership fix.
Unrelated versions/platforms receive no method changes. No global Qt files are
patched and no accessibility behavior or setting is disabled.

ConnectionSelector.cpp also studies qtbase v6.11.0
`src/widgets/accessible/itemviews.cpp:645-741` and
`src/plugins/platforms/cocoa/qcocoaaccessibilityelement.mm:219-226,257-267,342-362`
(reference only; no method bodies copied). Before a structural connection-list
update on Qt 6.11.0 Cocoa, it invokes the public table-interface ModelReset
handler to clear Qt's child-ID cache. Cocoa's deferred native-row cleanup can
already have deleted real cell interfaces while that cache retains their IDs;
RowsRemoved/RowsInserted dereference those absent interfaces. The item model,
surviving row objects and persistent indexes are not reset. Data-only refreshes
retain their accessibility IDs. The native regression reproduces the actual
ConnectionSelector removal after rebuilding native rows and draining its pool.
JJ Boyd (KG4VCF), 2026-10-03, with OpenAI Codex assistance.

src/gui/QtCocoaAccessibleTableCache.cpp studies the same qtbase v6.11.0 sources
(reference only; no method bodies copied). It is the shared form of the repair
above for item views whose rows change while the app runs: installed once on a
view (or a combo's popup view), it invokes the public table-interface
ModelReset handler on Qt 6.11.0 Cocoa, with accessibility active, before every
row or column insert or removal the view's model announces. A row change after
Cocoa expired the view's native rows otherwise dereferenced a deleted cell
interface inside QAccessibleTable::modelChange (the Spot List crash: a spot
from the Core arriving through SpotTableModel::addSpot). The item model,
proxies, selection and persistent indexes are not reset. Views using it: the
Spot List, FreeDV Reporter station table and message combo, ConnectionPanel
radio table and model combo, LAN scan table, DSP asset table and model combos,
notch, filter preset, transverter and needle calibration tables, HL2 I2C log,
This Core model combo, microphone, TX and PA profile combos, RX1 sample rate
combo and the antenna combos. The offscreen regressions delete the cell
interfaces through the public QAccessible API, the same cache call the Cocoa
bridge makes. JJ Boyd (KG4VCF), 2026-10-08, with Anthropic Claude Code
assistance.
