# Ideas

Things worth building that nobody has started. Each one carries what it would
cost, because on this board that is the deciding factor: there is about 200 KB
of firmware space and only about 39 KB of working memory free, and the screen
already takes 50 ms a frame.

Nothing here is a commitment. Cross one off or add one whenever.

## Games

The board already has most of the machinery a small game needs, which is why
these are cheap:

- **Rewards.** Outfits can be handed out for anything, not only detection
  counts: the werewolf, the gold toaster, the starfield eye and the lodge are
  all one-line unlocks, with the celebration screen already written.
- **A screen.** New full screens are a switch case and a draw function.
- **Touch.** Taps, holds and swipes are all handled.
- **Saving.** Settings storage keeps small things across restarts, and the
  black box (branch, untested) can keep bigger ones.
- **Detections and the clock.** Both are live, so a game can be about what is
  really around you, and about the real day.

### 1. DETECTION BINGO — the best of these

A card of detection types. Every type you actually detect marks its square.
Lines and a full card hand out rewards.

**Why it fits:** it makes the thing the board already does into a game, it
needs no new art, and it rewards going places. It also gives the rarer types
(RAVEN, ALPR, DEAUTH) a point, which today are just numbers that never move.

**Shape:**
- A 4x4 card, sixteen squares from the eighteen types.
- The card is drawn so it is winnable: mostly types this board has seen before
  (their lifetime counts are already stored), plus two or three it has not,
  as the stretch.
- A square marks on a first sighting, so sitting next to one device all day
  marks one square.
- A line is worth something small (a Squachy line, a toast, a stat). A full
  card unlocks an outfit — BINGO CARD, a loud holiday shirt, or a visor.
- A fresh card each week, with a streak count for weeks completed. Or the
  player asks for a new card, which resets the streak.
- Tapping a square says what that type is, which doubles as a way to learn
  the types.

**Cost:** about 20 bytes saved (the card, the marks, the day it was issued),
one screen of maybe 3-6 KB of firmware, no extra working memory, no per-frame
cost when the screen is closed. The marking itself is a few lines where a
detection is first logged.

**Open questions:** whether a full card is realistic outside a city, and
whether to count detections from squad members' boards too (their hellos
already carry what they have seen).

### 2. HUNT, scored

The HUNT screen already shows a live signal-strength gauge for one device.
Turn a hunt into a round: the clock starts when you pick a target, and stops
when you get within a set strength. It keeps your best time per type.

**Cost:** tiny, since the screen exists. A timer, one best-time table, and an
end-of-round card. Maybe 2 KB.

**Payoff:** it is the closest thing here to a real-world game, and it makes a
good clip — walking around while the bar climbs.

### 3. WHACK-A-TRACKER

Trackers pop up around the screen and you tap them before they leave. Uses
the detection icons that already exist. A round is 30 seconds.

**Cost:** 3-4 KB, a handful of bytes for the high score. No new art.

**Payoff:** pure filler, but it is the sort of thing people show other people.
Squachy can heckle you while you play.

### 4. SQUACHY SAYS

Four corners light in a sequence and you repeat it. Gets longer each round.

**Cost:** about 2 KB. It needs nothing but the screen and taps.

**Payoff:** universally understood, works on the desk while you sit there,
and an outfit at round 10 is a real reward.

### 5. A CLASSIC — snake or pong

Pong against Squachy (he leans and misses on purpose sometimes), or snake
that eats detections.

**Cost:** 3-5 KB each.

**Payoff:** low, honestly. They are fine, but they say nothing about what this
board is for, and everything above says something.

### 6. MORE CATCH EGGS

The pattern is already proven four times: something rare crosses a background
and catching it unlocks an outfit — the gold toaster, the starfield eye, the
lodge, the werewolf. Each new one is small and self-contained.

Candidates: a fish in the aquarium, a shooting star, a face in the terminal
log, something in the fire.

**Cost:** 1-2 KB each, no new screens, no saved state beyond the unlock bit.

### 7. TWO-BOARD GAMES OVER SQUACHMESH

Tic-tac-toe or battleship between two boards in range.

**Cost:** the honest one. The message format has no spare bytes, so this needs
its own frame type, and every change to the radio protocol needs two boards to
test. Days, not hours.

**Payoff:** high for two people who both own one, low for everybody else.
Worth keeping in mind for when squad messaging is finished, not before.

## Not games

- **Detection streaks.** Days in a row with at least one detection, and the
  longest run ever. Almost free, and it gives the daily lines something to
  talk about.
- **A day chart.** Detections per hour for the last month, drawn on the desk.
  Needs the black box, then it is tiny.
- **Devices seen before.** "This tag has been near you on four different
  days." The genuinely useful one, and the one to be careful with: many
  trackers change their address, so it has to be tested before it is claimed.

## Rules of thumb for anything here

- **Working memory, not firmware space, is the limit.** A game that needs a
  second screen buffer is not cheap.
- **A game must not stop detection.** The radios keep running, or the board
  stops being what it is.
- **It has to survive a restart** if it takes more than a minute to play.
- **The reward should be an outfit or a line from Squachy.** Those are already
  what the board gives out.
