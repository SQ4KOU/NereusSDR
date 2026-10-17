#import <AVFoundation/AVFoundation.h>

#include "core/LogCategories.h"
#include "core/MacMicPermission.h"
#include <QCoreApplication>
#include <QLoggingCategory>
#include <QThread>
#include <chrono>

namespace NereusSDR {

namespace {

using PermissionClock = std::chrono::steady_clock;

void logPermissionStage(const char* flow, const char* stage,
                        PermissionClock::time_point started,
                        PermissionClock::time_point eventTime,
                        const char* result = nullptr)
{
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
        eventTime - started).count();
    const auto monotonicUs = std::chrono::duration_cast<std::chrono::microseconds>(
        eventTime.time_since_epoch()).count();
    qCInfo(lcAudio).noquote().nospace() << "mic-permission flow=" << flow
                             << " stage=" << stage
                             << " pid=" << QCoreApplication::applicationPid()
                             << " thread=" << reinterpret_cast<quintptr>(QThread::currentThreadId())
                             << " monotonic-us=" << monotonicUs
                             << " elapsed-us=" << elapsedUs
                             << " result=" << (result ? result : "none");
}

const char* permissionName(MicPermission permission)
{
    switch (permission) {
    case MicPermission::Granted:      return "granted";
    case MicPermission::Denied:       return "denied";
    case MicPermission::Undetermined: return "undetermined";
    }
    return "unknown";
}

} // namespace

void requestMicrophonePermission()
{
    const auto started = PermissionClock::now();
    logPermissionStage("gui-launch", "status-enter", started, started);
    AVAuthorizationStatus status =
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    const auto statusReturned = PermissionClock::now();

    const char* statusName = "unknown";
    switch (status) {
    case AVAuthorizationStatusNotDetermined: statusName = "NotDetermined"; break;
    case AVAuthorizationStatusRestricted:    statusName = "Restricted";    break;
    case AVAuthorizationStatusDenied:        statusName = "Denied";        break;
    case AVAuthorizationStatusAuthorized:    statusName = "Authorized";    break;
    }
    logPermissionStage("gui-launch", "status-return", started, statusReturned, statusName);
    qCInfo(lcAudio) << "Microphone TCC status on launch:" << statusName;

    switch (status) {
    case AVAuthorizationStatusNotDetermined:
        logPermissionStage("gui-launch", "request-enter", started, PermissionClock::now());
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL granted) {
            const auto arrived = PermissionClock::now();
            if (granted) {
                qCInfo(lcAudio) << "Microphone access granted";
            } else {
                qCWarning(lcAudio) << "Microphone access denied by user";
            }
            logPermissionStage("gui-launch", "callback-arrived", started, arrived,
                               granted ? "granted" : "denied");
        }];
        logPermissionStage("gui-launch", "request-return", started, PermissionClock::now());
        break;

    case AVAuthorizationStatusAuthorized:
        break;

    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:
        qCWarning(lcAudio)
            << "Microphone access denied/restricted. Open System Settings → "
               "Privacy & Security → Microphone and enable access for NereusSDR.";
        break;
    }
}

namespace {

MicPermission toMicPermission(AVAuthorizationStatus status)
{
    switch (status) {
    case AVAuthorizationStatusAuthorized:    return MicPermission::Granted;
    case AVAuthorizationStatusNotDetermined: return MicPermission::Undetermined;
    case AVAuthorizationStatusDenied:
    case AVAuthorizationStatusRestricted:    return MicPermission::Denied;
    }
    return MicPermission::Denied;
}

} // namespace

MicPermission microphonePermissionStatus()
{
    return toMicPermission(
        [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]);
}

MicPermission requestMicrophonePermissionAndWait()
{
    const auto started = PermissionClock::now();
    logPermissionStage("helper", "status-recheck-enter", started, started);
    const MicPermission current = microphonePermissionStatus();
    logPermissionStage("helper", "status-recheck-return", started, PermissionClock::now(),
                       permissionName(current));
    if (current != MicPermission::Undetermined) {
        return current;
    }
    // The completion handler runs on an arbitrary dispatch queue, never
    // the calling thread, so waiting here cannot deadlock it.
    __block BOOL answer = NO;
    dispatch_semaphore_t answered = dispatch_semaphore_create(0);
    logPermissionStage("helper", "request-enter", started, PermissionClock::now());
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                             completionHandler:^(BOOL granted) {
        const auto arrived = PermissionClock::now();
        answer = granted;
        logPermissionStage("helper", "callback-arrived", started, arrived,
                           granted ? "granted" : "denied");
        dispatch_semaphore_signal(answered);
    }];
    logPermissionStage("helper", "request-return", started, PermissionClock::now());
    logPermissionStage("helper", "wait-enter", started, PermissionClock::now());
    dispatch_semaphore_wait(answered, DISPATCH_TIME_FOREVER);
    logPermissionStage("helper", "wait-return", started, PermissionClock::now());
#if !__has_feature(objc_arc)
    dispatch_release(answered);
#endif
    qCInfo(lcAudio) << "Microphone permission answered:"
                    << (answer ? "granted" : "denied");
    return answer ? MicPermission::Granted : MicPermission::Denied;
}

} // namespace NereusSDR
