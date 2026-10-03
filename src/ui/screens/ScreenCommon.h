#pragma once

#include "ui/screens/Screens.h"

namespace screens::detail {

    inline constexpr int16_t kBackButtonHeight = 36;

    Action navigation(ui::Context& ui, Screen active, Screen& screen);
    bool back(ui::Context& ui, ui::Rect rect);
    ui::Rect content(ui::Context& ui);
    ui::Rect tabContent(ui::Context& ui);

} // namespace screens::detail
