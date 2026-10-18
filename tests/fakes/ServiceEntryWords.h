#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// tests/fakes/ServiceEntryWords.h  (NereusSDR)
// =================================================================
//
// The words a background Core's startup entry would run, read back the way
// the system that owns the entry reads them. A test that compares these
// compares what really starts, not the text the entry was built from.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-12: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

namespace NereusSDR::Test {

// A systemd unit: the program and arguments of its ExecStart line. systemd
// groups words with double quotes, reads a backslash inside them as an
// escape, and takes "$$" for a dollar sign and "%%" for a percent sign
// (systemd.service(5), "Command lines"; systemd.unit(5), "Specifiers").
inline QStringList systemdCommandWords(const QByteArray& unit)
{
    QString line;
    for (const QString& candidate : QString::fromUtf8(unit).split(QLatin1Char('\n'))) {
        if (candidate.startsWith(QStringLiteral("ExecStart="))) {
            line = candidate.mid(10);
            break;
        }
    }
    QStringList words;
    QString word;
    bool quoted = false;
    bool open = false;
    for (qsizetype i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        const QChar next = i + 1 < line.size() ? line.at(i + 1) : QChar();
        if (quoted && ch == QLatin1Char('\\') && !next.isNull()) {
            word += next;
            ++i;
        } else if ((ch == QLatin1Char('$') || ch == QLatin1Char('%')) && next == ch) {
            word += ch;
            ++i;
        } else if (ch == QLatin1Char('"')) {
            quoted = !quoted;
            open = true;
        } else if (!quoted && ch.isSpace()) {
            if (open) {
                words << word;
            }
            word.clear();
            open = false;
        } else {
            word += ch;
            open = true;
        }
    }
    if (open) {
        words << word;
    }
    return words;
}

// A launchd agent: the strings of its ProgramArguments array.
inline QStringList launchAgentWords(const QByteArray& plist)
{
    QStringList words;
    QXmlStreamReader reader(plist);
    bool nextArrayIsTheCommand = false;
    bool inCommand = false;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QStringLiteral("key")) {
            nextArrayIsTheCommand = reader.readElementText() == QStringLiteral("ProgramArguments");
        } else if (reader.isStartElement() && reader.name() == QStringLiteral("array")) {
            inCommand = nextArrayIsTheCommand;
        } else if (reader.isEndElement() && reader.name() == QStringLiteral("array")) {
            inCommand = false;
        } else if (inCommand && reader.isStartElement()
                   && reader.name() == QStringLiteral("string")) {
            words << reader.readElementText();
        }
    }
    return words;
}

} // namespace NereusSDR::Test
