// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - tst_rade_channel: skeleton tests for the Phase 3R Task I1
// RadeChannel wrapper. Three contracts verified:
//
//   1. initialState              fresh RadeChannel reports !isActive() && !isSynced()
//   2. startStop                 start(<real path>) returns true and flips isActive()
//                                back to false after stop()
//   3. modelLoadFailureDisables  start(<nonexistent path>) returns false and leaves
//                                isActive() at false
//
// I1 ships the skeleton only. The DSP body (rade_open / LPCNet feature
// extractor / FARGAN vocoder / resamplers) lands in Phase 3R Task I2
// (RX path) and Task I3 (TX path); the embedded RADE text channel
// lands in Task I4. The tests below do not exercise any of those
// surfaces; they pin the lifecycle skeleton that I2-I4 layer onto.
//
// See src/core/RadeChannel.h for the upstream license headers and
// modification-history block (verbatim freedv-gui BSD-2-Clause-style
// header + AetherSDR project-level attribution per
// docs/attribution/HOW-TO-PORT.md rule 6).

#include <QtTest/QtTest>
#include <QTemporaryFile>

#include "core/RadeChannel.h"

using namespace NereusSDR;

class TestRadeChannel : public QObject {
    Q_OBJECT

private slots:
    void initialState();
    void startStop();
    void modelLoadFailureDisablesChannel();
};

void TestRadeChannel::initialState()
{
    // A freshly constructed RadeChannel must not advertise itself as
    // active or in sync. Active flips on start(); sync flips on the
    // RADE decoder's sync indication once I2 wires it up.
    RadeChannel ch;
    QVERIFY(!ch.isActive());
    QVERIFY(!ch.isSynced());
}

void TestRadeChannel::startStop()
{
    // Create a real fixture file so start()'s skeleton path-exists
    // check passes. The byte contents are irrelevant for I1; I2 will
    // teach start() to actually load the .f32 model via rade_open().
    QTemporaryFile fixture;
    QVERIFY(fixture.open());
    fixture.write("rade-model-skeleton-fixture");
    fixture.flush();

    RadeChannel ch;
    QVERIFY(ch.start(fixture.fileName()));
    QVERIFY(ch.isActive());

    ch.stop();
    QVERIFY(!ch.isActive());
}

void TestRadeChannel::modelLoadFailureDisablesChannel()
{
    // Nonexistent path must not flip isActive(). I2 will further
    // require the file's contents to be a valid RADE model and will
    // close + reset on rade_open() failure as well; I1 just guards
    // the path-exists precondition.
    RadeChannel ch;
    QVERIFY(!ch.start("/nonexistent/path.f32"));
    QVERIFY(!ch.isActive());
}

QTEST_GUILESS_MAIN(TestRadeChannel)
#include "tst_rade_channel.moc"
