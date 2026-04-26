#pragma once
// no-port-check: NereusSDR-original stub — no Thetis logic ported yet.
// Full class body (TXA pipeline, dsp.cs P/Invoke equivalents) arrives in
// 3M-1a Task C.2.  Attribution headers will be added at that point.

// =================================================================
// src/core/TxChannel.h  (NereusSDR)
// =================================================================
//
// Independently implemented stub.  No Thetis code is ported in this
// file.  This header exists solely to give std::unique_ptr<TxChannel>
// a complete type in WdspEngine.cpp (required by the unique_ptr
// destructor).  Task C.2 will populate the class body and add the
// appropriate Thetis attribution headers.
//
// Modification history (NereusSDR):
//   2026-04-25 — Stub created by J.J. Boyd (KG4VCF) during C.1 fixup.
// =================================================================

#include <QObject>

namespace NereusSDR {

// TX WDSP channel wrapper.
// Full implementation arrives in 3M-1a Task C.2 (TXA pipeline construction).
// This stub exists solely to make std::unique_ptr<TxChannel> legal in WdspEngine.
class TxChannel : public QObject {
    Q_OBJECT
public:
    explicit TxChannel(QObject* parent = nullptr) : QObject(parent) {}
    ~TxChannel() override = default;
};

} // namespace NereusSDR
