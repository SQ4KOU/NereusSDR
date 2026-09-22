#pragma once
// =================================================================
// src/core/dsp/DspAssetValidation.h  (NereusSDR)
// =================================================================
// Bounded, non-actuating validators for station-owned WDSP assets.
// The source-format derivation and upstream licence notices are in the
// implementation file.
//
// Modification history (NereusSDR):
//   2026-09-21 — Created for NereusSDR by J.J. Boyd (KG4VCF), with
//                AI-assisted implementation via OpenAI Codex.
// =================================================================

#include <QByteArray>
#include <QString>

namespace NereusSDR {

enum class DspAssetKind {
    NnrModel,
    Ps3Correction,
};

QString dspAssetKindName(DspAssetKind kind);
bool dspAssetKindFromName(const QString& name, DspAssetKind* kind);

struct DspAssetValidationResult {
    bool accepted{false};
    DspAssetKind kind{DspAssetKind::NnrModel};
    QString error;
    QString format;
    int version{0};
    QString compatibility;
    qint64 size{0};
    QString hashHex;

    // Format-specific accepted metadata. Unused fields remain zero/empty.
    int tensorCount{0};
    QString numericEncoding;
    quint64 decodedBytes{0};
    int curveCount{0};
    int branchCount{0};
    int pointCount{0};
};

class DspAssetValidation final
{
public:
    static constexpr qint64 kMaxNnrModelBytes = 64LL * 1024 * 1024;
    static constexpr qint64 kMaxPs3CorrectionBytes = 1LL * 1024 * 1024;
    static constexpr qint64 kTransferChunkBytes = 64LL * 1024;
    static constexpr int kMaxPs3BranchesPerCurve = 16;
    static constexpr int kMaxPs3PointsPerCurve = 1614;

    static DspAssetValidationResult validate(DspAssetKind kind, const QByteArray& bytes);
    static DspAssetValidationResult validateNnrModel(const QByteArray& bytes);
    static DspAssetValidationResult validatePs3Correction(const QByteArray& bytes);

    // capacityBytes includes the fixed WDSP C buffer's terminating NUL.
    static bool validateEncodedPath(const QString& path, qsizetype capacityBytes,
                                    QString* error = nullptr);
};

} // namespace NereusSDR
