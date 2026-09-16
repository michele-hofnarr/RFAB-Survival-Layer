#include "PCH.h"

#include "Core/Notify.h"

#include "Settings.h"

namespace RSL
{
    namespace
    {
        // A notification is not a widget anyone has to build. RFAB's HUD is the
        // vanilla one here: Debug.Notification reaches HUDMenu.ShowMessage,
        // which pushes onto Messages.MessageArray, and the clip that renders it
        // sets tf1.html = true and assigns htmlText. Whatever arrives is parsed
        // as HTML, so a colour is a tag around the string.
        //
        // That same reading explains why RFAB never seems to lose one:
        // MessageArray is an unbounded queue with no de-duplication, and
        // Messages.MAX_SHOWN lets four stand on screen at once, so a burst
        // stacks instead of overwriting.
        //
        // The colour is the player's, from the MCM. Read on every message
        // rather than cached, so the picker's result shows on the next one.
        [[nodiscard]] std::string Colourise(std::string_view a_text)
        {
            return fmt::format("<font color='#{:06X}'>{}</font>",
                Settings::uNotifyColour & 0xFFFFFFu, a_text);
        }
    }

    Notify& Notify::GetSingleton()
    {
        static Notify singleton;
        return singleton;
    }

    void Notify::Push(std::string a_text)
    {
        if (a_text.empty()) {
            return;
        }
        std::scoped_lock guard(_lock);
        _queue.push_back(std::move(a_text));
    }

    void Notify::Push(RE::BGSMessage* a_message)
    {
        if (!a_message) {
            logger::warn("notify: null message");
            return;
        }

        // A MESG carries its text in DESC; FULL is empty on these.
        RE::BSString description;
        a_message->GetDescription(description, a_message);
        std::string text = description.c_str() ? description.c_str() : "";

        if (text.empty()) {
            // Never silently swallow one: an empty read is a bug worth seeing,
            // and the editor id at least tells the player something happened.
            const char* editorID = a_message->GetFormEditorID();
            logger::error("notify: no text on {} ({:#08x})",
                editorID ? editorID : "?", a_message->GetFormID());
            text = editorID ? editorID : "";
        }

        logger::info("notify: queued \"{}\"", text);
        Push(std::move(text));
    }

    void Notify::Drain()
    {
        // Nothing sent while the HUD is down would survive, so hold it instead
        // of spending it. This also keeps messages out of loading screens and
        // menus, where they would be invisible.
        auto* ui = RE::UI::GetSingleton();
        if (!ui || !ui->IsMenuOpen(RE::HUDMenu::MENU_NAME) || ui->GameIsPaused()) {
            std::scoped_lock guard(_lock);
            if (!_queue.empty()) {
                logger::info("notify: holding {} message(s), HUD not ready", _queue.size());
            }
            return;
        }

        // No pacing of our own. v0.4.0 calls Message.Show() and is done, and
        // the reason that never loses one is visible in the HUD's own code:
        // Messages.MessageArray is an unbounded queue and MAX_SHOWN lets four
        // stand at once, so a burst stacks rather than overwriting. Spacing
        // them here only delayed them.
        std::string text;
        {
            std::scoped_lock guard(_lock);
            if (_queue.empty()) {
                return;
            }
            text = std::move(_queue.front());
            _queue.pop_front();
        }

        // Explicitly not cancelling on a queued duplicate. That default is what
        // silently swallowed messages in the first place, and the HUD stacks
        // four at once, so there is nothing to protect them from.
        RE::DebugNotification(Colourise(text).c_str(), nullptr, false);
        logger::info("notify: shown \"{}\"", text);
    }

    void Notify::Clear()
    {
        std::scoped_lock guard(_lock);
        _queue.clear();
    }
}
