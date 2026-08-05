#pragma once
// =================================================================
// src/core/session/IStationLink.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 4.
//
// Non-owning seam RadioModel holds (attachStation / detachStation,
// RadioModel.h) for the as-yet-unbuilt control-plane link a later task's
// StateMirror will drive: pushing slice/meter/status state out to a
// remote GUI and applying command verbs it sends back, over whatever
// transport Task 18's wss session provides. See docs/architecture/
// 2026-07-28-remote-daemon-architecture-design.md and the R2 plan.
//
// This task only establishes the attach point -- nothing calls a method
// on it yet, so the interface is deliberately empty beyond a virtual
// destructor. Adding methods before a real call site exists would be
// exactly the speculative-surface trap core/spectrum/ISpectrumSink.h's
// own header comment warns against ("Do not add methods speculatively:
// find a real call site first"). A later task (StateMirror, Task 7/8)
// grows this interface when it has an actual push/apply call to make.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-04  J.J. Boyd / KG4VCF  Remote daemon R2 Task 4: station-link
//                                    seam. AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

namespace NereusSDR {

/// Non-owning control-plane seam. RadioModel holds a pointer to one via
/// attachStation()/detachStation() without owning or including anything
/// about the transport underneath it; concrete implementations live
/// above src/core (a future StateMirror-backed session object).
class IStationLink {
public:
    virtual ~IStationLink() = default;
};

} // namespace NereusSDR
