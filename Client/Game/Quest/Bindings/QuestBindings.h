#pragma once

class asIScriptEngine;

namespace fable::game
{
    class QuestService;
}

namespace fable::scripting::bindings
{
    bool RegisterQuestBindings(asIScriptEngine& engine, game::QuestService& quests);
}
