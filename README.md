# mod-realm-calendar

A standalone AzerothCore module that maintains a machine-readable JSON feed of the realm's **public in-game calendar events** for launchers, websites, and other external consumers.

The module intentionally excludes player-created calendar events, raid lockouts, guild invitations, character reminders, and other personal calendar data. Editorial/admin news belongs in a separate realm-news project.

## Automatic rolling publication

Normal operation requires no GM command and no cron job. When enabled, the module:

- publishes `calendar.json` automatically when `worldserver` starts;
- checks periodically for a month rollover;
- regenerates when a new month begins, dropping the expired month and adding another month at the far end;
- republishes after a configuration reload;
- writes to a temporary file and replaces the public JSON only after the new file is complete.

`RealmCalendar.FutureMonths = 12` means **the current month plus twelve additional months**. This matches the observed 3.3.5a calendar horizon: on September 4, 2026 the published range is September 1, 2026 through September 30, 2027. On October 1 it becomes October 1, 2026 through October 31, 2027.

If the server is offline at midnight on the first, nothing special is required: the next startup immediately publishes the correct new rolling range.

## Calendar sources

The public schedule is built from the same data AzerothCore uses:

- seasonal/date-driven holidays use the in-memory `Holidays.dbc` definitions after AzerothCore has populated their dynamic dates;
- weekly fishing contests use their `GameEventMgr` recurrence for timing while retaining their holiday identity;
- weekly recurrence is advanced in realm wall-clock time so 14:00 events remain at 14:00 across DST transitions;
- Call to Arms / `CalendarFilterType 2` events are excluded from the default public feed.

The September 2026 regression set was verified against the WoW 3.3.5a client for Darkmoon Faire, Kalu'ak Fishing Derby, Stranglethorn Fishing Extravaganza, Pirates' Day, Brewfest, and Harvest Festival.

## Configuration

AzerothCore installs `conf/mod_realm_calendar.conf.dist` through the normal module configuration process.

```ini
RealmCalendar.Enable = 1
RealmCalendar.OutputFile = "calendar.json"
RealmCalendar.FutureMonths = 12
RealmCalendar.CheckIntervalMinutes = 60
RealmCalendar.DefaultHorizonDays = 30
RealmCalendar.MaxHorizonDays = 730
```

For a web-served feed, set `RealmCalendar.OutputFile` to an absolute path writable by the worldserver service account, for example a directory exposed by nginx or Apache. Parent directories are created automatically when possible.

## JSON schema

The generated document uses `schemaVersion: 1` and records when it was generated, its inclusive date range, and the public events. Multi-day holidays are represented as date-only/all-day events; contests preserve explicit timestamps with the realm's UTC offset.

Example shape:

```json
{
  "schemaVersion": 1,
  "generatedAt": "2026-09-05T02:00:00Z",
  "range": {
    "start": "2026-09-01",
    "end": "2027-09-30"
  },
  "events": [
    {
      "holidayId": 424,
      "gameEventId": 64,
      "name": "Kalu'ak Fishing Derby",
      "category": "fishing",
      "allDay": false,
      "start": "2026-09-12T14:00:00-0400",
      "end": "2026-09-12T15:00:00-0400",
      "texture": "Calendar_FishingExtravaganza"
    }
  ]
}
```

## Diagnostic/admin commands

These commands are not required for normal publication:

- `.realmcalendar publish` — force an immediate JSON regeneration.
- `.realmcalendar month <year> <month>` — compact client-style month view for regression testing.
- `.realmcalendar upcoming [days]` — calculated public occurrences in a future window.
- `.realmcalendar holidays` — logical `Holidays.dbc` definitions and stages.
- `.realmcalendar inspect` — raw calendar-facing `GameEventMgr` rows.

## Installation

Place the repository under AzerothCore's `modules` directory, rerun CMake, rebuild/install, configure the output path, and restart `worldserver`.

```bash
cd ~/azerothcore-wotlk/build
cmake ..
make -j"$(nproc)"
make install
```

After startup, check the worldserver log for a `[Realm Calendar] Published ...` line and inspect the configured JSON file.

## Scope

`mod-realm-calendar` publishes realm/public calendar events only and stays consumer-neutral: Portalkeeper, a website, or another launcher can all consume the same feed.

## License

GPL-2.0-or-later, matching AzerothCore's licensing model.
