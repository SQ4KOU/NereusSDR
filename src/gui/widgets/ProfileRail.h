// src/gui/widgets/ProfileRail.h

// --- From src/gui/widgets/ProfileRail.h ---
// =================================================================
// src/gui/widgets/ProfileRail.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// ── Die Schiene ──────────────────────────────────────────────────────
//
// OE5SOS, 2026-08-15:
//
//   „Wenn dies alles erledigt ist, soll die Möglichkeit entstehen, ein
//    zweites Profil oder auch ein drittes anzulegen, welches ich mir
//    selbst wieder individuell gestalten kann."
//
// In der Vorlage sind das runde Abzeichen am linken Rand — ein Buchstabe je
// Arbeitsfläche, darunter ein gestricheltes Plus. Genau so hier.
//
// ── Warum ein Buchstabe und nicht der Name ───────────────────────────
//
// Die Schiene ist 44 Pixel breit; ein Name passt nicht hinein. Das
// Abzeichen trägt den ersten Buchstaben, der volle Name steht im
// Tooltip und im Rechtsklickmenü. Das ist kein Kompromiss, sondern der
// Punkt: die Schiene soll man mit dem Augenwinkel lesen, nicht
// studieren.
//
// ── Was die Schiene NICHT tut ────────────────────────────────────────
//
// Sie schaltet nicht von selbst um. LayoutProfiles::profileFor() kann
// ein Profil an Band und Modus binden, und die Verdrahtung wäre ein
// Signalanschluss — sie bleibt aus, weil der Betreiber es so
// entschieden hat (2026-08-15): „Nein, nur von Hand. Kein Fenster, das
// sich beim Bandwechsel unter dir verändert."
//
// Die Fenstergröße gehört ebenfalls nicht dazu, aus demselben Grund:
// ein Profilwechsel soll Panels tauschen und nicht das Fenster
// umspringen lassen.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-15 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// src/gui/widgets/ProfileRail.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/widgets/ProfileRail.h
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banner is copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR. Differences from upstream:
//   - The rail reads NereusSDR's WorkspaceProfiles instead of Longpath's
//     LayoutProfiles. Profiles have stable ids, so each badge is keyed by
//     the profile id, and the signals' QString argument carries that id
//     where upstream carried the name. The signal set is unchanged.
//   - importRequested() no longer means "replace this profile": the
//     receiver imports into a new profile (unified workspace design).
//   - manageRequested() (NereusSDR) opens the manage dialog from the
//     context menu.
//   - setPlacementToggleOffered() (NereusSDR) can leave the "move the
//     rail" item out of the context menu.
//   Workspace preview only.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//                 Rewired to WorkspaceProfiles (ids, not names).
// =================================================================

#pragma once

#include <QHash>
#include <QPointer>
#include <QString>
#include <QWidget>

class QBoxLayout;
class QPushButton;

namespace NereusSDR {

class WorkspaceProfiles;

// From Longpath src/gui/widgets/ProfileRail.h:58 [@551576e3]
class ProfileRail : public QWidget {
    Q_OBJECT
public:
    /// Waagrecht oder senkrecht. Senkrecht ist die Schiene am linken
    /// Rand, wie seit 2026-08-15; waagrecht sitzt sie in der
    /// Kommandoleiste neben der Rate.
    ///
    /// Betreiber am 2026-10-06: „meine profile links im eck sollten oben
    /// in die taksleite neben 48 khz". Es ist DASSELBE Bauteil, nur
    /// anders gelegt -- damit bleiben Rechtsklickmenue, das X am
    /// Abzeichen und das gestrichelte Plus erhalten, statt sie in einer
    /// zweiten Fassung nachzubauen.
    /// Die Richtung hat KEINEN Vorgabewert, mit Absicht: mit einem
    /// band der alte Aufruf ProfileRail(profiles, parent) das Elternteil
    /// stillschweigend an die Richtung, und der Fehler faellt erst im
    /// Bild auf. Ohne Vorgabe meldet ihn der Uebersetzer
    /// (2026-10-06 in tst_menus_of_native_fields genau so passiert).
    explicit ProfileRail(WorkspaceProfiles* profiles,
                         Qt::Orientation richtung,
                         QWidget* parent = nullptr);

    /// Wie die Schiene gerade liegt.
    Qt::Orientation richtung() const { return m_richtung; }

    /// Abzeichen neu aufbauen. Wird auf profilesChanged() gerufen.
    void rebuild();

    // ── Für Tests ────────────────────────────────────────────────────

    /// Die Namen der Abzeichen, von oben nach unten.
    // NereusSDR: the profile ids, in order.
    QStringList badges() const;
    /// Das Umhaengen melden, ohne das Menue zu oeffnen -- damit ein
    /// Pruefstand den INHALT der Meldung sehen kann und nicht nur, dass
    /// es das Signal gibt.
    void meldeUmhaengenForTest()
    {
        emit placementToggleRequested(m_richtung == Qt::Horizontal
                                          ? Qt::Vertical : Qt::Horizontal);
    }

    /// Der Name des hervorgehobenen Abzeichens, oder leer.
    // NereusSDR: the active profile id.
    QString activeBadge() const;
    /// Ein Abzeichen anklicken. false, wenn es das nicht gibt.
    bool clickBadge(const QString& id);
    /// Der Beschriftungsbuchstabe eines Namens — auch für Namen, die
    /// mit Leerzeichen oder einem Sonderzeichen anfangen.
    static QString initialFor(const QString& name);
    // NereusSDR: the badge button of a profile id, or null (tests and
    // the preview's crops).
    QPushButton* badgeButton(const QString& id) const { return m_badges.value(id, nullptr); }
    QPushButton* plusButton() const { return m_plus; }
    // NereusSDR: whether the context menu offers to move the rail. The
    // workspace preview keeps the rail in the top strip, so it is off there.
    void setPlacementToggleOffered(bool offered) { m_placementToggleOffered = offered; }

    // From Longpath src/gui/widgets/ProfileRail.h:106-107 [@551576e3]
    static constexpr int kWidth      = 44;
    static constexpr int kBadgeSide  = 30;
    /// Die Hoehe der SCHIENE in der Leiste. Dort steht sie neben den
    /// Pillen von CommandBar, und Scheiben von 30 px zwischen
    /// Rechtecken liessen sich als Fremdkoerper lesen -- genau das war
    /// am 2026-10-07 der Befund am Bildschirm.
    ///
    /// Es sind zwei Pixel mehr als CommandBar::kPillHeight, und das ist
    /// kein Versehen: in Qt gilt `min-height` im Stilblatt fuer die
    /// INHALTSflaeche, der 1 px starke Rand kommt oben und unten dazu.
    /// Eine Pille mit kPillHeight = 27 ist also 29 Pixel hoch. Die
    /// Schiene auf 27 festzunageln hiesse, die Abzeichen zwei Pixel
    /// flacher als ihre Nachbarn zu machen.
    ///
    /// Die Abzeichen selbst bekommen hier KEINE Hoehe gesetzt -- die
    /// kommt aus pillStyle(), damit sie gar nicht erst abweichen kann.
    /// tst_profilschiene_richtung stellt Schiene, Abzeichen und eine
    /// echte Pille nebeneinander, damit diese Zahl nicht stillschweigend
    /// verrutscht.
    // From Longpath src/gui/widgets/ProfileRail.h:125 [@551576e3]
    static constexpr int kLeistenSeite = 29;

signals:
    /// Der Betreiber will ein neues Profil. Den Namen erfragt der
    /// Empfänger — die Schiene kennt keinen Dialog, damit sie ohne
    /// Oberfläche prüfbar bleibt.
    /// Der Betreiber will die Schiene woanders haben. Die Schiene legt
    /// sich NICHT selbst um -- sie haengt im Aufbau des Hauptfensters,
    /// und ein Bauteil, das sich im Rechtsklick selbst umhaengt, waere
    /// an zwei Stellen gleichzeitig zustaendig. Also meldet sie nur.
    void placementToggleRequested(Qt::Orientation neueRichtung);

    void newProfileRequested();
    void renameRequested(const QString& id);
    void duplicateRequested(const QString& id);
    void removeRequested(const QString& id);
    /// Betreiber 2026-08-28: "vielleicht sollte es ein profil speichern
    /// auch geben" -- der jetzige Stand sichert sich sonst nur beim
    /// Umschalten oder Beenden. Wer ohne Verbindung gestaltet und gleich
    /// wissen will, dass es sitzt, statt der Beenden-Sequenz zu
    /// vertrauen, kann hier von Hand nachhelfen.
    void saveRequested(const QString& id);
    /// Betreiber 2026-08-30: "sollten auf dem Desktop zur Sicherheit
    /// abspeicherbar sein" -- eine Sicherung neben AppSettings' XML, die
    /// man sehen, verschieben und einem Backup beilegen kann. Die Schiene
    /// kennt wie bei saveRequested() keinen Dateidialog; das erledigt der
    /// Empfaenger.
    void exportRequested(const QString& id);
    /// Betreiber 2026-08-30: "DIE gespeicherten profile sollte man auch
    /// mit rechter moustaste importiren können" -- das Gegenstueck zu
    /// exportRequested(). Traegt den Namen wie exportRequested(): ein
    /// Import ERSETZT den Zustand DIESES Profils (Betreiber, selber
    /// Tag: "wenn ich importiere will ich es nicht als neues profil
    /// importiren").
    // NereusSDR: the receiver imports into a NEW profile (unified
    // workspace design); the id says which badge was right-clicked.
    void importRequested(const QString& id);
    // NereusSDR: "Manage profiles..." from the context menu.
    void manageRequested();

private:
    void showMenuFor(const QString& id, const QPoint& globalPos);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    // Je Abzeichen ein Loeschkreuz, sichtbar beim Ueberfahren.
    QHash<QString, QPushButton*> m_closers;

    QPointer<WorkspaceProfiles> m_profiles;
    /// Quer zur Laufrichtung mittig: senkrecht waagrecht zentriert,
    /// waagrecht senkrecht zentriert.
    Qt::Alignment mittig() const
    {
        return m_richtung == Qt::Vertical ? Qt::AlignHCenter : Qt::AlignVCenter;
    }

    /// Die Kantenlaenge eines Abzeichens am jetzigen Platz. Am Rand die
    /// Scheibe, in der Leiste die Pillenhoehe -- siehe kLeistenSeite.
    int seite() const
    {
        return m_richtung == Qt::Vertical ? kBadgeSide : kLeistenSeite;
    }

    /// Hoehe und Breite eines Knopfes am jetzigen Platz. Begruendung
    /// an der Umsetzung -- kurz: in der Leiste darf die Breite NICHT
    /// fest sein, sonst schneidet der geerbte Innenabstand die Schrift ab.
    void setzeMasse(QPushButton* b) const;

    QBoxLayout*  m_column{nullptr};   // VBox oder HBox, je nach Richtung
    Qt::Orientation m_richtung{Qt::Vertical};
    QPushButton* m_plus{nullptr};
    QHash<QString, QPushButton*> m_badges;
    QStringList m_order;
    bool m_placementToggleOffered{true};   // NereusSDR
};

} // namespace NereusSDR
