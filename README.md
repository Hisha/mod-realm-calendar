# mod-realm-calendar

A standalone AzerothCore module for exposing the realm's **public in-game calendar events** to external consumers such as launchers and websites.

This module intentionally does **not** expose player-created calendar events, raid lockouts, guild invitations, or other personal calendar data.

## Current milestone: diagnostic discovery

The first version is deliberately diagnostic. Before publishing JSON, the module needs to prove that it can reproduce the same public event schedule shown by the WoW 3.3.5a calendar.

It reads AzerothCore's already-loaded `GameEventMgr` data and treats entries with a non-zero `HolidayId` as calendar-facing public events.

### Commands

```text
.realmcalendar inspect
.realmcalendar upcoming
.realmcalendar upcoming 365
```

`inspect` displays the raw calendar-facing `GameEventMgr` definitions that AzerothCore loaded, including:

- game event ID
- holiday ID
- holiday stage
- game-event state
- calculated start and absolute end
- recurrence interval
- event length
- active state
- description

`upcoming [days]` calculates event occurrences from the loaded schedule. The default horizon is 30 days; the diagnostic command allows up to 730 days so a full in-game calendar year can be compared.

## Why this module reads GameEventMgr

The goal is to stay aligned with the same server-side event schedule AzerothCore uses rather than maintaining a second hand-written holiday calendar. AzerothCore exposes `GetEventMap()` and `GetActiveEventList()` through `GameEventMgr`, and its loaded `GameEventData` includes recurrence, length, holiday ID, stage, and description.

This first pass is intentionally not the final exporter. If the diagnostic output differs from the dates/times visible in the client calendar, the next step is to trace AzerothCore's dynamically populated `Holidays.dbc` data and make the exporter consume the exact calendar-facing representation.

## Initial acceptance test

September 2026 is the first regression month. Compare the command output against the actual client calendar for events such as:

- Darkmoon Faire
- Kalu'ak Fishing Derby
- Stranglethorn Fishing Extravaganza
- Pirates' Day
- Brewfest
- Harvest Festival

The dates **and times** should agree with the game before JSON publication is implemented.

## Configuration

AzerothCore installs the module configuration from:

```text
conf/mod_realm_calendar.conf.dist
```

On install, the normal AzerothCore module-config process creates/uses `mod_realm_calendar.conf` under the server module config directory. Current options are:

```ini
RealmCalendar.Enable = 1
RealmCalendar.DefaultHorizonDays = 30
RealmCalendar.MaxHorizonDays = 730
```

The horizon settings control the diagnostic `upcoming` command now and will also provide the basis for the later JSON export horizon.

## Installation

Clone or copy the repository under the AzerothCore `modules` directory, rerun CMake, rebuild, and install as usual.

Example:

```bash
git clone <repository-url> ~/azerothcore-wotlk/modules/mod-realm-calendar
cd ~/azerothcore-wotlk/build
cmake ..
make -j"$(nproc)"
make install
```

Then restart `worldserver` and test with:

```text
.realmcalendar inspect
.realmcalendar upcoming 30
.realmcalendar upcoming 365
```

## Planned next milestones

Once the diagnostic output matches the in-game calendar:

1. Define a stable `calendar.json` schema.
2. Export approximately 12 months of public realm events by default.
3. Preserve exact event start/end timestamps where the game exposes them.
4. Publish the JSON atomically to a configurable filesystem location for nginx/Apache or another static web server.
5. Keep the module consumer-neutral: Portalkeeper, websites, and other launchers can all consume the same feed.

## Scope

`mod-realm-calendar` is for **realm/public events only**. Editorial server announcements and administrator news belong in a separate realm-news module.

## License

GPL-2.0-or-later, matching AzerothCore's licensing model.


## Diagnostic commands

- `.realmcalendar inspect` — raw calendar-facing `GameEventMgr` rows with canonical event IDs taken from the event-map index.
- `.realmcalendar holidays` — logical holiday definitions from the in-memory `Holidays.dbc` store, including stages, packed dates, filter type, looping flag, and texture.
- `.realmcalendar upcoming [days]` — logical public occurrences; seasonal holidays use Holidays.dbc while weekly fishing contests use the GameEventMgr recurrence.
- `.realmcalendar month <year> <month>` — compact month view for direct comparison with the in-game calendar. Call to Arms / CalendarFilterType 2 events are excluded from the default public view.

The current development target is to make `upcoming` agree with the public events shown by the WoW 3.3.5a calendar before adding JSON export.
