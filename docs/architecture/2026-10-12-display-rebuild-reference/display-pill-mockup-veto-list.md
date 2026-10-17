# Display pill rebuild mockup: calls made without you, for your veto

Written 2026-10-11 by the display pill session, brought up to date 2026-10-12. The mockup
is one page you can click:
`/Users/j.j.boyd/NereusSDR/.claude/worktrees/display-spectrum-settings-debug-8a645c/.crew/2026-10-10-display-pill-and-levels-plan/mockups/display-all-pages.html`
It is the shape you picked ("1 full rebuild, mock up every page."): the pill and Setup's
Display pages drawn as the same controls from one list. Nothing here is built yet.

Since 2026-10-12 09:00 EDT the page to judge from is the live one, in the same folder:
`display-pill-live.html`. Its pans draw a made-up band from the settings beside them, so
every row shows its effect as you change it. The first page is kept unchanged beside it.
The last section of this list, "The live page", holds what the live page added or changed.

Saying nothing about one means it stands. 66 items, the ten you are most likely to care
about first. Three questions are still open and are not in this list; the mockup's dashed
bar at the top lets you try both answers to each.

## The ten most likely to matter to you

1. **The pill has five tabs: Spectrum, Levels, Waterfall, View, FFT.** Today it has
   Spectrum, FFT, Waterfall, 3D View. Levels is new and holds both level jobs, the spectrum
   scale and the waterfall levels, so cause and effect are on one tab. Each tab is the Setup
   page of the same name, and "Setup" at the pill's foot opens that page on that pan. One
   exception: the Noise Floor, Bin Width and Peak Value buttons are on the pill's Spectrum
   tab, as today, and their Setup rows are on Peaks & Markers, beside the detail rows each
   one switches. The note under the buttons says so and links there. I kept it that way
   because a switch and the rows it grays belong on one Setup page, and the pill has no
   Peaks & Markers tab; say so if you would rather move them.
2. **Every picture setting is per pan, with "All pans" in Setup as the master.** That takes
   in the ones that are every-pan only today: the Noise Floor line, Bin Width, Peak Value,
   the peak trace and markers, dB Step, floor tracking and Clarity. A dot marks a row a pan
   has set for itself. Click the dot and the row follows All pans again.
3. **One waterfall levels chooser: Clarity, Auto, Floor, Manual, Scale.** It replaces the
   Clarity switch, AGC, NF-AGC and "Use spectrum min/max". It starts on Clarity, as the app
   ships. Low is gray in every mode but Manual and shows the level in use. Today Low is
   gray only while AGC sets it, and keeps showing the stored number.
4. **One spectrum scale chooser: Manual or Track floor.** "Maintain grid range" becomes
   "Keep range". Floor Offset and Keep range sit at the chooser's own level, not indented
   under it: an indent is kept for a row that depends on a switch, a list or the picture
   row above it.
5. **dB Max, dB Min and 3D Floor stay per band, with a Band picker in Setup.** With an open
   pan picked, the picker is fixed to that pan's band; under All pans and on a pan that is
   not open it is free. Setup's "Spectrum scale" group carries the band in its header, but
   only dB Max and dB Min are the band's, so a line under dB Min says so: "dB Max and dB
   Min are kept per band (10 m). Manual or Track floor, Floor Offset and Keep range are the
   same on every band." Whether a pan may have its own scale per band is a separate
   question coming to you.
6. **On the pan that is transmitting, every Levels row is gray.** Each shows the transmit
   value and says where that is set. Today only dB Max and dB Min go gray.
7. **Trace Color and Fill Color move from Appearance to Spectrum,** where each pan has its
   own. In the pill they are swatches at the head of the Line Width and Fill Alpha rows.
   In Setup, Fill Color sits at the Fill box's own level, not indented under it, because
   it also colors the trace until Trace Color is given a color of its own. Appearance
   keeps its other colors. The peak trace's color row is "Peak Color:".
8. **One name per control, the same in the pill and in Setup.** Where the pill has the row
   today its short name wins, so about 25 Setup labels get shorter: "Spectrum Detector:"
   becomes "Detector:", "Color Scheme:" "Scheme:", "Update Period:" "Rate:", "Low
   Threshold:" "Low:", "Depth:" "Rewind:", "Show grid" "Grid", and so on. The pill's
   "Line:", "Fill:" and "Scale text:" become "Line Width:", "Fill Alpha:" and "Scale Text:".
   "Normalize trace" becomes "Normalize to 1 Hz", the name it has in Thetis, on the receive
   row and on the transmit row (which reads "1 Hz BW: Av / Sa" today). When it is gray it
   says "Normalize to 1 Hz needs the Average, Sample or RMS detector."
9. **The reason a row is gray is on the screen, not in a tooltip.** In Setup it sits beside
   the row: the pill's whole sentence, never a shorter form, so a long one makes its row
   taller (the tallest is five lines, on Levels). A gray chooser
   has its sentence under it. In the pill a fixed three-line area at the bottom shows
   the reason for the row under the pointer, and every pill sentence is written to fit
   those three lines.
10. **A child row is gray whenever its parent is off, which adds gray states the app does
    not have today.** Fill Alpha with Fill off; Fill Color with Fill off, but only once
    Trace Color has a color of its own (until then the trace is drawn in Fill Color, so
    the row stays live and a gray row never moves the picture); Grid Spacing and dB Step
    with Grid off; Avg Time with averaging at None; the NB threshold with NB Blank off;
    Time Zone with Timestamp at None; the Background rows with Background off, with no
    picture, or in the 3D view; the 3D rows in the 2D view; the rows under Peak trace, Peak
    markers, Peak Value and Noise Floor when each is off. Each says why. The same goes
    for what the 3D view does not draw: Line Width, Trace Color, Gradient, Fill, Fill
    Alpha, Fill Color, Peak Hold, Peak Delay and Zero line are gray in the 3D view and
    say to pick 2D Waterfall. Today they stay live there and do nothing. In Setup's
    Background group the order is the Background box, BG Color under it, Picture at the
    box's own level (choosing a picture turns Background on, as today), and BG Opacity
    under Picture.

## The pill

11. **Three pill rows share a line with their own on/off button:** Peak Hold with its
    delay, NB Blank with its threshold, Grid with its spacing. The line's label is gray
    exactly when its button is, not when only the slider beside it is. Where a pan has its
    own value for the second control on such a line, the dot's sentence names that control
    ("Pan 3's own Peak Delay."); the Background line's dot says "Pan 3's own Picture." Every
    other dot says "Pan 3's own value." The other way is "own value" on every dot.
12. **Normalize joins the pill,** as a button directly under Detector, beside Fill and
    Gradient. Rows that are only in the pill today get Setup rows.
13. **Setup only, no pill row:** waterfall detector, timestamp time zone, dB Step, label
    alignment, the dBm strip and zero line switches, and the peak and marker details.
14. **The pill's foot: "Follow all pans (n)" replaces "Reset this pan",** and "Setup"
    replaces "Setup defaults". n counts every value the pan has of its own, and the bottom
    area says how many of those are on rows only Setup shows. The button keeps one width
    whatever the count, so nothing at the foot moves. In Setup, the line at the top
    of a pan's page always carries "Follow all pans" and "Use Pan N's on all pans", gray
    when there is nothing of the pan's own on that page. "Use Pan N's on all pans" gives
    All pans the pan's own values on that page and drops every pan's own value for those
    rows. Where it copies one end of a slider pair and not the other, the copied end stops
    at the pair's gap from the All pans other end, so All pans never ends up with Low over
    High.
15. **The four choosers (waterfall levels, spectrum scale, view, sizing) are a row of
    buttons with no label,** and a line under each says what the choice does. Today the view
    is a drop-down labeled "Spectrum:" and Sizing has a label. Three of today's FFT
    sentences name that label, so with it gone they are reworded: "Sizing is Auto, so the
    zoom sets the FFT size." becomes "Auto is chosen, so the zoom sets the FFT size.",
    the Hz/bin one likewise, and "Choose Hz/bin under Sizing to set the bin width
    yourself." becomes "Choose Hz/bin above to set the bin width yourself." The other way
    is to keep the "Sizing" label on that one chooser and today's words with it; a
    reviewer raised it, so say if you would rather.
16. **The Clarity light, the small "C", sits beside Re-tune** on the pill's Levels tab, with
    today's three states and colors. As today it is green only while Clarity has a level to
    work from, so in a window that is not connected it is gray and says why, and it is
    amber on every Clarity pan while the radio transmits.
17. **The FFT tab's header tag reads "STATION".** Today it reads "ALL PANS".
18. **The custom gradient button is gray, not hidden, when it cannot act.** The noise floor
    text position row stays on screen, gray, saying it is not used.

## Setup

19. **In Setup a number row is the pill's slider with the number beside it.** Today many
    Setup rows are a number box alone. Rate steps by 10 ms in both places; Setup steps by 1
    today.
20. **Setup's pan buttons show which slice each pan carries, or "not open",** on a second
    line. A pan that is not open is dimmed but can still be set up.
21. **With no pan open, Setup's band rows stay live,** because Setup has its own Band
    picker. Today dB Max, dB Min, dB Step and the copy button are gray until a pan is open.
22. **Grid Spacing and dB Step are children of Grid.**
23. **A row is gray in Setup wherever it is gray in the pill, with the same sentence:**
    Decimation (not connected, older Core), 3D Span and Slice Shadow (limited graphics),
    Peak Delay (Peak Hold off). Today Setup leaves these live.
24. **Under All pans, or on a pan that is not open, a gray row whose level is each pan's own
    shows no number.** It reads "per pan" or "not open", with no slider handle, and says who
    sets it. That is Low and High outside Manual, and dB Max and dB Min under Track floor.
    It holds under Scale too: pans may be on different bands, so the picked band's dB Max
    and dB Min are not every pan's Low and High. A reviewer would rather show the picked
    band's pair there; say so if you agree. In a window that is not connected nothing is
    measured, so those rows read "no signal", in the pill as well. Under Scale, when a
    trim has moved Low or High off the scale's number, the gray row says so: "Low is the
    spectrum's dB Min, trimmed by Black Level." On a narrow scale the trims can bring the
    scale's top to within 5 dB of Low; High is then kept 5 dB above Low and its row says
    that instead: "High is the spectrum's dB Max, but is kept 5 dB above Low, at -35 dBm
    for now. Pick Manual to set High here."
25. **The two reset buttons set what they set today, and each says which rows on other
    pages it also sets.** "Reset to Smooth Defaults" gets a group of its own at the foot of
    Spectrum; with one waterfall chooser it turns Manual into Auto and leaves Clarity,
    Floor and Scale alone. Pressed under All pans it follows your answer to the first open
    question: with "every pan takes it" it reaches every pan, with "keeps" each pan's own
    values stay, as today. "Reset 3D to defaults" stays live on a computer with limited
    graphics. The two copy buttons copy the numbers their source rows show, and are gray
    where either row shows none; the sentence names only the end that shows none ("Low
    is set pan by pan here, so there is no one pair to copy."). What a copy writes is
    held to what the target rows can be set to by hand, -200 to 0 dBm and the pair's gap,
    so a copy from a level in use outside that range (a High of 18 dBm, say) lands on
    the nearest pair the rows can hold. They are also gray outside Manual, where the rows
    they write are not yours to set, and each says who is setting them and to pick Manual.
    "Copy from waterfall levels" is gray under Scale as well: there the waterfall's Low and
    High come from the spectrum scale, so copying them back, with a trim moved, would shift
    the scale again on every press. It says to pick Manual for the waterfall levels. Today both
    are live in every mode and write numbers that nothing uses until you pick Manual.
26. **Spectrum keeps its two link buttons, "Configure peaks" and "Configure multimeter".**
    Peaks & Markers keeps its link back, now "← Spectrum".
27. **Multimeter is the page the app has today,** same rows, ranges and groups. Three
    changes: its link back reads "← Spectrum", each row of the first group says who holds
    it, the station or this computer (and keeps saying so when the row is gray), and the
    other two groups carry the THIS COMPUTER tag.
28. **The waterfall "Delay" readout becomes a sentence under Rate** on Setup's Waterfall
    page.
29. **"Get Monitor Hz" sits under FPS, the row it sets,** and is gray whenever FPS is. Today
    it is beside thread priority.

## Transmitting

30. **While a pan transmits, a row with no transmit counterpart keeps showing its stored
    receive value,** gray, and says it is not used while transmitting.
31. **If you choose "each pan's own" for the transmit scale, those rows are on Levels,** and
    Transmit Display says so and links there. Their group sits after Waterfall levels, so
    the only dB Max and dB Min above the waterfall rows are the spectrum scale's.
32. **Transmit Display says who holds each group.** The nine analyzer rows are the
    station's. The transmit waterfall's Low, High, Palette and Low Color belong to this
    computer, as today. The transmit scale rows are new.

## Remote windows and limited graphics

33. **"Not connected" exists only for a remote window.** A window that runs the radio
    itself never grays the station's rows. In a remote window without its Core the
    station's rows are gray with the app's sentence, and dB Max and dB Min are gray too,
    because the Core keeps each band's scale (its dB Max and dB Min). 3D Floor stays live.
34. **A remote window grays Cal Offset and Thread Priority and says the Core sets them.** On
    an older Core it also grays Decimation and the Transmit Display analyzer rows.
35. **On a computer with limited graphics the gray 3D Span row shows 0 % and Slice Shadow
    shows off,** the values in use.

## Readings and numbers

36. **One reading per control, the same in the pill and in Setup:** 3D Gain, Span and Angle
    in %, FPS in fps, Hz/bin as "N.N Hz", dB Max and dB Min in dBm, Rewind's choices spelled
    out ("60 seconds", "5 minutes"). Offsets read as today, without a plus sign. Black
    Level and Color Gain read as today ("N+3 dB", "38 dB"), except outside Clarity when the
    "work in every mode" box is checked, where they read as a signed trim in dB. An
    untouched trim then reads "+0 dB"; a reviewer would rather it read "0 dB" like the
    offsets; say so if you agree. Because the reading goes with the mode, a pan on Scale
    can show "Black Level: +21 dB" with "All pans: N+3 dB" under it when All pans is on
    Clarity: each is right for its own mode, but the two cannot be compared. A reviewer
    flagged it for you. I left it, because the line shows the All pans value as the All
    pans page shows it; the other way is to read it in the pan's own mode ("All pans:
    +0 dB"), which is what the row would show if the pan followed All pans.
37. **Cal Offset reads in dB in both places.** Today the pill says dB and Setup says dBm.
38. **Rewind and 3D Speed show their real result and name Rate as what sets it.** 3D Speed
    reads "Match" or "1:N".
39. **Avg Time and WF Avg Time sliders reach 500 ms** in the pill and in Setup; the number
    box takes up to 9999 ms. A number past 500 ms (the smooth reset sets 650 ms) leaves
    the handle at the end of the track, and it keeps that number until the handle is
    dragged back onto the track. Pressing any slider's handle without dragging it changes
    nothing.
40. **The FFT readout line is today's,** and adds a sentence when Hz/bin is held at the
    smallest or the largest size.

## The dot and the slider pairs

41. **How the dot behaves under each answer to the first open question.** "Every pan takes
    it": a pan row set back to the All pans value follows All pans again. "Keeps": a row
    changed on a pan stays the pan's own, even at the same value, until its dot is clicked.
    Under either answer a click that changes nothing does nothing: picking the choice a pan
    already shows gives it no dot. In the pill the dot sits in the pill's left margin
    beside the label, so a label keeps its whole column and does not move when its dot
    comes or goes. A chooser's dot sits in the same margin. A button's dot and a color
    swatch's dot sit on the top right corner. Nothing else in the pill moves when a dot
    comes or goes.
42. **A pan's own Low and the All pans High must never cross, and the mockup does not
    hold them apart.** A slider pair is held to its gap (10 dB for dB Max and dB Min, 5
    for Low and High) when both ends are the pan's own or both follow All pans. When a
    pan has its own Low and follows All pans for High, or the reverse, the app has to
    pick which end gives way. My pick for the design: the end being moved stops at the
    gap, and the other end stays where it is. The same holds for the two ends of a pan's
    own transmit scale. The mockup's note says it does not draw this. The All pans pair
    itself is always held, after "Use Pan N's on all pans" too.
43. **When a measured level rises to the number you set for the other end, your end is
    carried above it, and its row says so.** Under Track floor without Keep range, dB Min
    is measured and dB Max is yours. Under Auto and Floor, Low is measured and High is
    yours. If the measured end comes to within the pair's gap of your number, your row
    shows a number one gap above it (10 dB for dB Max, 5 for High) and the pan is drawn
    with that. Your own number is kept and comes back when the measured end drops. While
    it is held the row says so: "dB Max is set to -118 dBm and is kept at least 10 dB
    above dB Min. Track floor has dB Min at -98 dBm, so dB Max is at -88 dBm for now."
    A press on your end stops one gap from what the other row shows. It also stops one
    gap from the number kept for the gray end, the one that end goes back to under
    Manual, so the pair is never crossed when you change the chooser; no sentence says so
    when that is the nearer stop. The other way to build it is to stop the measured end
    one gap under your number. I carried your end instead, so that "dB Min is the noise
    floor plus Floor Offset" stays true. Today the app has no hold here: Track floor
    leaves dB Max where it is, so dB Min can come up to it or pass it.

## Dropped, not drawn, and elsewhere

44. **Dropped, each with something that carries the same information:** Setup's live bin
    width number and the receive FFT group's "Bin Width (Hz)" and "FFT Size" readouts (the
    FFT readout line); "Editing per-band grid" and "Open a panadapter to set its band's dB
    Max and dB Min." (the Band picker); the pill's "BG Color:" row (its swatch heads BG
    Opacity); the pill's band note on a transmitting pan; Transmit Display's "FFT Size"
    readout (the row above it shows the same number).
45. **Not drawn in the mockup, and kept in the app:** tooltips on rows that are not gray,
    Setup's hints that point to other parts of Setup, the moment a remote window has
    connected and the Core has not yet sent its settings, and Appearance's remaining
    colors. A chosen background picture is a stand-in too: a band of colors, a different
    one for each of the three picture names.
46. **The Display applet and the phone follow the same control list.** They are not drawn.

## The live page (display-pill-live.html, 2026-10-12)

Where an item here differs from one above, this one is what the live page does.

47. **Setup's tree holds only the pages this work changes:** Display's eight and
    Appearance, Colors & Theme. A note under the tree says the other pages are left out.
48. **Colors & Theme keeps its nine other colors and loses Trace Color and Fill Color** to
    Display, Spectrum, with a link there. "Reset all colors to defaults" sets those nine
    only; a pan's trace, fill and background colors are not touched by it. This replaces
    the "Appearance's remaining colors are not drawn" part of item 45.
49. **The receive Custom scheme gets an editor.** Today it has none and looks like Default.
    Setup's Waterfall page gains "Edit custom gradient..." under Scheme, gray unless Scheme
    is Custom. It opens a color strip in place: click adds a color, drag moves one,
    double-click changes one, right-click removes one, and the two end colors stay at the
    ends. One gradient for every pan. Transmit Display's button opens the same strip for
    the transmit palette.
50. **Track floor holds dB Min 10 dB under dB Max.** When the noise floor plus Floor Offset
    would come closer than that, dB Min stops and its row says so. This is the other way
    round from item 43, which carried dB Max up. It matches the fix waiting on the branch
    `claude/track-floor-holds-db-min`. Floor Offset acts as you move it.
51. **Rows that draw nothing in the app today show what they are meant to do:** Normalize
    to 1 Hz moves the trace, Avg Time sets how many frames Time Window averages, Show zero
    line draws on every pan, and Track floor is each pan's own.
52. **Zoom on a pan:** the S, B, - and + buttons, Ctrl or Cmd with the wheel, and a drag
    along the frequency scale. The plain wheel scrolls the web page here; in the app it
    tunes.
53. **Of a pan's button column only the collapse arrow and Display are drawn.**
54. **The pill opens over its pan, where the app opens it,** on Pan 1 when the page loads.
    Display opens and closes it. It can cover part of the pan below.
55. **The pan that transmits shows 8 kHz around its slice.**
56. **3D Span over 100 % fills the overhang with each row's quiet level.**
57. **A number box can be typed into,** except Black Level, Color Gain, 3D Speed and FFT
    Size, whose readings are not plain numbers.
58. **"Get Monitor Hz" reads this screen's real refresh rate** and holds it to the row's
    10 to 60.
59. **A chooser or color box opens above its button when there is no room below.**
60. **Background pictures are real pictures** (three built into the page, or one you browse
    for). This replaces the "band of colors" stand-in in item 45.
61. **"Things to try" starts folded,** one step per line.

Made-up signals, so that every row has something to show:

62. **Each band carries static crashes,** bright rows across the whole waterfall every 5 to
    11 seconds on 40 m and less often on the other two, sized so NB Blank at its 6 dB
    default takes most of them out.
63. **Pans 1 and 2 each carry one strong steady carrier near the band edge.** Without it
    the app's blanker rule, copied here as it is written, blanked the loudest voice
    signal's rows. That may be a fault in the app; it is reported separately.
64. **The Fast Attack color shows for 1 second** after keying, un-keying and a new FFT
    Size. In the app a band change and a jump of more than 500 kHz start it too; the page
    has neither.
65. **Rewind's shorter choices differ only once the page has run longer than them.** The
    line under Rewind shows the depth at once.
66. **Two rows draw nothing by nature:** Thread Priority and Noise Floor Text Position.
