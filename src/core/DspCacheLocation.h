#pragma once
// no-port-check: NereusSDR-original. Where a process keeps its saved FFT plans.

#include <QString>
#include <QStringList>

namespace NereusSDR::DspCacheLocation {

// The folder this process keeps its saved FFT plans (WDSP's wdspWisdom
// file) and its impulse cache in. Building the plans takes minutes, and a
// radio connection waits for them.
//
// Left alone it is the program's own configuration folder, which carries
// the program's name: the app and nereusd each have their own. A background
// Core started by the app is the app's own Core carried on, so the app
// names its folder in the entry that starts it (StationServiceManager) and
// nereusd takes it from --dsp-cache-dir. Without that the background Core
// built every plan again while the radio waited.
QString directory();

// Names the folder for this process; an empty value goes back to the
// program's own. Called at startup, before a radio connects.
void setDirectory(const QString& directory);

// The file a profile keeps its impulse cache in, inside directory(). Every
// desktop profile and the background Core share that folder, but each
// profile has settings of its own, so each has a file of its own: one
// profile's close never deletes the cache another saved. The default
// profile keeps the name it always had, impulse_cache.bin; a named profile
// adds its name, impulse_cache-<profile>.bin. A name that is not a valid
// profile name (AppSettings::isValidProfileName) is the default profile,
// as it is for the settings folder.
QString impulseCacheFileName(const QString& profile);

// Every profile's impulse cache file in a folder, by file name. They were
// all built on the folder's saved FFT plans, so all of them go when the
// plans are built again.
QStringList impulseCacheFileNames(const QString& folder);

} // namespace NereusSDR::DspCacheLocation
