// NereusSDR-original infrastructure — no Thetis source ported here.
// No upstream attribution required (NereusSDR filter-response test).
//
// Design note (Task 1.5):
//   RxChannel::filterResponseMagnitudes(nPoints) synthesizes the FIR bandpass
//   filter magnitude response using WDSP's fir_bandpass() with the same
//   parameters the WDSP channel uses internally.  The response is computed by
//   zero-padding the real taps into a double-precision FFTW3 FFT buffer,
//   executing a forward FFT, and returning dB magnitudes for the positive
//   half-spectrum sampled at nPoints.
//
//   The test constructs an RxChannel on channel 99 (never opened in WDSP),
//   sets filter edges via the carry-only setFilterLow/setFilterHigh setters,
//   then verifies that filterResponseMagnitudes() returns exactly nPoints
//   values and that the passband bin exceeds the out-of-band bin.
//
//   The test is guarded with HAVE_WDSP + HAVE_FFTW3: without both, the method
//   returns an empty vector and the test verifies that contract instead.
//
//   Bin index math:
//     Nyquist = sampleRate / 2 = 24000 Hz (at 48 kHz)
//     bin = floor(freq / nyquist * (nPoints / 2))
//     passBandBin = floor(1450 / 24000 * 256) = floor(15.47) = 15
//     outsideBin  = floor(5000 / 24000 * 256) = floor(53.33) = 53
//   (512 nPoints → positive half = 256 unique bins, indexed [0..511])

#include <QtTest/QtTest>
#include "core/RxChannel.h"

using namespace NereusSDR;

static constexpr int kTestChannel  = 99;  // Never opened via OpenChannel
static constexpr int kTestBufSize  = 64;
static constexpr int kTestRate     = 48000;

class TestFilterHighResolution : public QObject {
    Q_OBJECT

private slots:
    void returns_n_points_of_magnitude_data()
    {
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);

        // setFilterLow/setFilterHigh are carry-only — no WDSP call on ch 99.
        ch.setFilterLow(200);
        ch.setFilterHigh(2700);

        const QVector<float> mag = ch.filterResponseMagnitudes(512);

#if defined(HAVE_WDSP) && defined(HAVE_FFTW3)
        QCOMPARE(mag.size(), 512);

        // Passband center ≈ 1450 Hz.
        // Bin = floor(freq / (sampleRate/2) * (nPoints/2))
        //   passBandBin = floor(1450 / 24000 * 256) = 15
        //   outsideBin  = floor(5000 / 24000 * 256) = 53
        const int passBandBin = 15;
        const int outsideBin  = 53;

        qDebug() << "passband[" << passBandBin << "] =" << mag[passBandBin]
                 << " outside[" << outsideBin << "] =" << mag[outsideBin];

        // The passband magnitude should be substantially higher than the
        // out-of-band magnitude.  A well-designed windowed-sinc BPF achieves
        // ~60–80 dB stop-band rejection; a 10 dB margin is generous.
        QVERIFY(mag[passBandBin] > mag[outsideBin] + 10.0f);
#else
        // Without WDSP + FFTW3, the method returns an empty vector.
        QCOMPARE(mag.size(), 0);
#endif
    }

    void npoints_zero_returns_empty()
    {
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        const QVector<float> mag = ch.filterResponseMagnitudes(0);
        QCOMPARE(mag.size(), 0);
    }

    void npoints_negative_returns_empty()
    {
        RxChannel ch(kTestChannel, kTestBufSize, kTestRate);
        const QVector<float> mag = ch.filterResponseMagnitudes(-1);
        QCOMPARE(mag.size(), 0);
    }
};

QTEST_MAIN(TestFilterHighResolution)
#include "tst_filter_high_resolution.moc"
