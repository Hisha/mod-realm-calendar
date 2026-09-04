#include "Chat.h"
#include "CommandScript.h"
#include "Config.h"
#include "GameEventMgr.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace Acore::ChatCommands;

namespace
{
struct RealmCalendarConfig
{
    bool Enabled = true;
    uint32 DefaultHorizonDays = 30;
    uint32 MaxHorizonDays = 730;
};

RealmCalendarConfig gRealmCalendarConfig;

void LoadRealmCalendarConfig()
{
    gRealmCalendarConfig.Enabled = sConfigMgr->GetOption<bool>("RealmCalendar.Enable", true);
    gRealmCalendarConfig.DefaultHorizonDays = std::max<uint32>(1, sConfigMgr->GetOption<uint32>("RealmCalendar.DefaultHorizonDays", 30));
    gRealmCalendarConfig.MaxHorizonDays = std::max<uint32>(gRealmCalendarConfig.DefaultHorizonDays,
        sConfigMgr->GetOption<uint32>("RealmCalendar.MaxHorizonDays", 730));
}

struct CalendarOccurrence
{
    uint32 EventId = 0;
    uint32 HolidayId = 0;
    uint8 HolidayStage = 0;
    std::string Description;
    time_t Start = 0;
    time_t End = 0;
};

std::string FormatLocalTime(time_t value)
{
    if (value <= 0)
        return "n/a";

    std::tm tmValue{};
#if defined(_WIN32)
    localtime_s(&tmValue, &value);
#else
    localtime_r(&value, &tmValue);
#endif

    std::ostringstream out;
    out << std::put_time(&tmValue, "%Y-%m-%d %H:%M");
    return out.str();
}

std::string StateName(GameEventState state)
{
    switch (state)
    {
        case GAMEEVENT_NORMAL:           return "normal";
        case GAMEEVENT_WORLD_INACTIVE:   return "world-inactive";
        case GAMEEVENT_WORLD_CONDITIONS: return "world-conditions";
        case GAMEEVENT_WORLD_NEXTPHASE:  return "world-nextphase";
        case GAMEEVENT_WORLD_FINISHED:   return "world-finished";
        case GAMEEVENT_INTERNAL:         return "internal";
        default:                         return "unknown";
    }
}

bool IsCalendarFacing(GameEventData const& eventData)
{
    return eventData.HolidayId != HOLIDAY_NONE && !eventData.Description.empty();
}

void AppendOccurrencesForEvent(GameEventData const& eventData, time_t windowStart, time_t windowEnd,
                               std::vector<CalendarOccurrence>& output)
{
    if (!IsCalendarFacing(eventData) || eventData.Start <= 0 || eventData.Length == 0)
        return;

    time_t const durationSeconds = static_cast<time_t>(eventData.Length) * 60;
    time_t const firstStart = eventData.Start;
    time_t const absoluteEnd = eventData.End > 0 ? eventData.End : std::numeric_limits<time_t>::max();

    if (eventData.Occurence == 0)
    {
        time_t const occurrenceEnd = std::min(firstStart + durationSeconds, absoluteEnd);
        if (occurrenceEnd >= windowStart && firstStart <= windowEnd)
        {
            output.push_back({ eventData.EventId, static_cast<uint32>(eventData.HolidayId), eventData.HolidayStage,
                               eventData.Description, firstStart, occurrenceEnd });
        }
        return;
    }

    time_t const periodSeconds = static_cast<time_t>(eventData.Occurence) * 60;
    if (periodSeconds <= 0)
        return;

    // Begin at the occurrence that starts at or immediately before the requested window.
    int64 occurrenceIndex = 0;
    if (windowStart > firstStart)
        occurrenceIndex = static_cast<int64>((windowStart - firstStart) / periodSeconds);

    if (occurrenceIndex > 0)
        --occurrenceIndex; // include an event that began before the window but is still active in it

    for (;; ++occurrenceIndex)
    {
        time_t const occurrenceStart = firstStart + static_cast<time_t>(occurrenceIndex) * periodSeconds;
        if (occurrenceStart > windowEnd || occurrenceStart >= absoluteEnd)
            break;

        time_t const occurrenceEnd = std::min(occurrenceStart + durationSeconds, absoluteEnd);
        if (occurrenceEnd < windowStart)
            continue;

        output.push_back({ eventData.EventId, static_cast<uint32>(eventData.HolidayId), eventData.HolidayStage,
                           eventData.Description, occurrenceStart, occurrenceEnd });
    }
}

std::vector<CalendarOccurrence> BuildOccurrences(uint32 horizonDays)
{
    std::vector<CalendarOccurrence> result;

    time_t const now = std::time(nullptr);
    time_t const windowEnd = now + static_cast<time_t>(horizonDays) * 24 * 60 * 60;

    GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
    for (GameEventData const& eventData : events)
        AppendOccurrencesForEvent(eventData, now, windowEnd, result);

    std::sort(result.begin(), result.end(), [](CalendarOccurrence const& left, CalendarOccurrence const& right)
    {
        if (left.Start != right.Start)
            return left.Start < right.Start;
        if (left.EventId != right.EventId)
            return left.EventId < right.EventId;
        return left.HolidayStage < right.HolidayStage;
    });

    return result;
}
}


class realm_calendar_configscript : public WorldScript
{
public:
    realm_calendar_configscript() : WorldScript("realm_calendar_configscript", { WORLDHOOK_ON_BEFORE_CONFIG_LOAD }) { }

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        LoadRealmCalendarConfig();
    }
};

class realm_calendar_commandscript : public CommandScript
{
public:
    realm_calendar_commandscript() : CommandScript("realm_calendar_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable realmCalendarCommandTable =
        {
            { "upcoming", HandleUpcomingCommand, SEC_ADMINISTRATOR, Console::Yes },
            { "inspect",  HandleInspectCommand,  SEC_ADMINISTRATOR, Console::Yes }
        };

        static ChatCommandTable commandTable =
        {
            { "realmcalendar", realmCalendarCommandTable }
        };

        return commandTable;
    }

private:
    static bool HandleUpcomingCommand(ChatHandler* handler, Optional<uint32> horizonDaysArg)
    {
        if (!gRealmCalendarConfig.Enabled)
        {
            handler->SendSysMessage("[Realm Calendar] Module is disabled in mod_realm_calendar.conf.");
            return true;
        }

        uint32 horizonDays = horizonDaysArg.value_or(gRealmCalendarConfig.DefaultHorizonDays);
        horizonDays = std::clamp(horizonDays, uint32(1), gRealmCalendarConfig.MaxHorizonDays);

        std::vector<CalendarOccurrence> const occurrences = BuildOccurrences(horizonDays);

        handler->PSendSysMessage("[Realm Calendar] Public holiday/game-event occurrences for the next {} day(s):", horizonDays);

        if (occurrences.empty())
        {
            handler->SendSysMessage("[Realm Calendar] No calendar-facing occurrences were found in that window.");
            return true;
        }

        for (CalendarOccurrence const& occurrence : occurrences)
        {
            bool const active = occurrence.Start <= std::time(nullptr) && occurrence.End >= std::time(nullptr);
            handler->PSendSysMessage(
                "[{}] event={} holiday={} stage={} {} -> {}{}",
                occurrence.Description,
                occurrence.EventId,
                occurrence.HolidayId,
                uint32(occurrence.HolidayStage),
                FormatLocalTime(occurrence.Start),
                FormatLocalTime(occurrence.End),
                active ? " [ACTIVE]" : "");
        }

        handler->PSendSysMessage("[Realm Calendar] {} occurrence(s) found.", occurrences.size());
        return true;
    }

    static bool HandleInspectCommand(ChatHandler* handler)
    {
        if (!gRealmCalendarConfig.Enabled)
        {
            handler->SendSysMessage("[Realm Calendar] Module is disabled in mod_realm_calendar.conf.");
            return true;
        }

        GameEventMgr::GameEventDataMap const& events = sGameEventMgr->GetEventMap();
        GameEventMgr::ActiveEvents const& activeEvents = sGameEventMgr->GetActiveEventList();

        uint32 count = 0;
        handler->SendSysMessage("[Realm Calendar] Calendar-facing GameEventMgr entries:");

        for (GameEventData const& eventData : events)
        {
            if (!IsCalendarFacing(eventData))
                continue;

            bool const active = activeEvents.find(static_cast<uint16>(eventData.EventId)) != activeEvents.end();
            handler->PSendSysMessage(
                "event={} holiday={} stage={} state={} start={} end={} every={}m length={}m{} :: {}",
                eventData.EventId,
                static_cast<uint32>(eventData.HolidayId),
                uint32(eventData.HolidayStage),
                StateName(eventData.State),
                FormatLocalTime(eventData.Start),
                FormatLocalTime(eventData.End),
                eventData.Occurence,
                eventData.Length,
                active ? " [ACTIVE]" : "",
                eventData.Description);
            ++count;
        }

        handler->PSendSysMessage("[Realm Calendar] {} calendar-facing event definition(s) found.", count);
        return true;
    }
};

void Addmod_realm_calendarScripts()
{
    new realm_calendar_configscript();
    new realm_calendar_commandscript();
}
