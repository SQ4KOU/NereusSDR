// tests/fakes/ConnectableRadioModel.cpp
//
// no-port-check: NereusSDR-original test fixture. Wires together two
// already-existing production classes (RadioModel, P1FakeRadio) for test
// use; no Thetis logic is ported or reimplemented here.

#include "ConnectableRadioModel.h"

#include <QtTest/QtTest>

namespace NereusSDR::Test {

ConnectableRadioModel::~ConnectableRadioModel() = default;

std::unique_ptr<ConnectableRadioModel> ConnectableRadioModel::create(int timeoutMs)
{
    // ConnectableRadioModel's constructor is private (see the header), so
    // std::make_unique can't reach it from outside the class; new + wrap
    // is the standard workaround for a factory that IS a member function.
    std::unique_ptr<ConnectableRadioModel> harness(new ConnectableRadioModel());

    harness->m_fake = std::make_unique<P1FakeRadio>();
    harness->m_fake->start();

    // Mirrors tst_p1_loopback_connection.cpp's makeInfo() -- see
    // task-2-controller-notes.md "Wiring the fake".
    harness->m_info.address         = harness->m_fake->localAddress();
    harness->m_info.port            = harness->m_fake->localPort();
    harness->m_info.boardType       = HPSDRHW::HermesLite;
    harness->m_info.protocol        = ProtocolVersion::Protocol1;
    harness->m_info.macAddress      = QStringLiteral("aa:bb:cc:11:22:33");
    harness->m_info.firmwareVersion = 72;
    harness->m_info.name            = QStringLiteral("ConnectableRadioModel fake");

    harness->m_model = std::make_unique<NereusSDR::RadioModel>();

    // Arm the synchronous test-only WdspEngine init path BEFORE calling
    // connectToRadio(). Order matters: connectToRadio() wires its
    // RX/TX-channel-creation lambda to WdspEngine::initializedChanged()
    // and only THEN calls initialize() unconditionally, so whichever path
    // initialize() takes has to already be selected by the time that call
    // happens. See WdspEngine::setSynchronousInitForTest()'s doc comment
    // for the full reasoning (including why a flag consulted inside
    // initialize() is required instead of pre-setting m_initialized).
    harness->m_model->wdspEngine()->setSynchronousInitForTest(true);

    harness->m_model->connectToRadio(harness->m_info);

    NereusSDR::RadioModel* const model = harness->m_model.get();
    const bool reachedConnected = QTest::qWaitFor(
        [model]() {
            return model->connectionState() == NereusSDR::ConnectionState::Connected;
        },
        timeoutMs);

    if (!reachedConnected) {
        // harness's destructor tears down both m_model and m_fake in the
        // right order (see the header's member-order comment) -- a timed-
        // out connect leaks neither the socket nor the RadioModel.
        return nullptr;
    }

    return harness;
}

} // namespace NereusSDR::Test
