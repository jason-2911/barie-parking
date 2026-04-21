/**
 * Theme.h — Neo-brutalist kiosk design tokens.
 */

#pragma once
#include <QString>

namespace theme {

// ─── Body / panel backgrounds ────────────────────────────────────────────────
inline constexpr auto BG_BODY        = "#e9e9e9";   // grey page bg
inline constexpr auto BG_LEFT        = "#fff7ed";   // left panel (orange-50)
inline constexpr auto BG_WHITE       = "#ffffff";
inline constexpr auto BG_ZINC100     = "#f4f4f5";
inline constexpr auto BG_ZINC200     = "#e4e4e7";
inline constexpr auto BG_ZINC900     = "#18181b";
inline constexpr auto BG_ZINC950     = "#09090b";   // near-black, border colour
inline constexpr auto BG_SUCCESS     = "#ebfff0";   // success body
inline constexpr auto BG_SUCCESS_LT  = "#f3fff6";   // success right panel

// ─── Header greens ────────────────────────────────────────────────────────────
inline constexpr auto GREEN_HEADER   = "#179c4a";
inline constexpr auto GREEN_CLOCK    = "#0f7d3a";

// ─── Accent – orange ─────────────────────────────────────────────────────────
inline constexpr auto ORANGE         = "#f97316";
inline constexpr auto ORANGE_LIGHT   = "#fff7ed";
inline constexpr auto ORANGE_100     = "#ffedd5";

// ─── Success – emerald ────────────────────────────────────────────────────────
inline constexpr auto SUCCESS        = "#16a34a";   // green-600
inline constexpr auto SUCCESS_DARK   = "#059669";   // emerald-600
inline constexpr auto SUCCESS_LIGHT  = "#d1fae5";
inline constexpr auto SUCCESS_TEXT   = "#065f46";   // emerald-800
inline constexpr auto SUCCESS_TILE   = "#ecfdf5";

// ─── Danger – red ────────────────────────────────────────────────────────────
inline constexpr auto DANGER         = "#dc2626";
inline constexpr auto DANGER_DARK    = "#b91c1c";
inline constexpr auto DANGER_LIGHT   = "#fef2f2";

// ─── Text ─────────────────────────────────────────────────────────────────────
inline constexpr auto TEXT_BLACK     = "#09090b";
inline constexpr auto TEXT_DARK      = "#18181b";
inline constexpr auto TEXT_GRAY      = "#52525b";
inline constexpr auto TEXT_MUTED     = "#71717a";

// ─── Border (heavy = 4 px near-black) ────────────────────────────────────────
inline constexpr auto HEAVY_BORDER   = "#09090b";
inline constexpr int  BORDER_W       = 4;

// ─── Plate ────────────────────────────────────────────────────────────────────
inline constexpr auto PLATE_YELLOW   = "#fbbf24";
inline constexpr auto PLATE_BG       = "#18181b";

// ─── Spacing ─────────────────────────────────────────────────────────────────
inline constexpr int SPACE_XS  =  4;
inline constexpr int SPACE_SM  =  8;
inline constexpr int SPACE_MD  = 16;
inline constexpr int SPACE_LG  = 24;
inline constexpr int SPACE_XL  = 32;
inline constexpr int SPACE_2XL = 48;

// ─── Radius (neo-brutalist = 0 inside, outer shell only) ─────────────────────
inline constexpr int RADIUS_SM  =  0;
inline constexpr int RADIUS_MD  =  0;
inline constexpr int RADIUS_LG  =  0;
inline constexpr int RADIUS_XL  =  0;
inline constexpr int RADIUS_2XL =  0;

// Kept for ScreenHelpers compat
inline constexpr auto BG_SURFACE   = BG_WHITE;
inline constexpr auto BG_ELEVATED  = BG_ZINC100;
inline constexpr auto BORDER        = HEAVY_BORDER;
inline constexpr auto BORDER_SUBTLE = HEAVY_BORDER;
inline constexpr auto BORDER_STRONG = HEAVY_BORDER;
inline constexpr auto ACCENT        = ORANGE;
inline constexpr auto ACCENT_SOFT   = ORANGE_LIGHT;
inline constexpr auto ACCENT_BORDER = ORANGE;
inline constexpr auto ACCENT_LIGHT  = ORANGE_100;
inline constexpr auto ACCENT_DARK   = "#c2410c";
inline constexpr auto SUCCESS_SOFT  = SUCCESS_TILE;
inline constexpr auto SUCCESS_BORDER= SUCCESS;
inline constexpr auto DANGER_SOFT   = DANGER_LIGHT;
inline constexpr auto DANGER_BORDER = DANGER;
inline constexpr auto DANGER_TEXT   = DANGER_DARK;
inline constexpr auto WARNING       = ORANGE;
inline constexpr auto WARNING_SOFT  = ORANGE_LIGHT;

// ─── Global QSS ───────────────────────────────────────────────────────────────
inline QString globalStylesheet() {
    return QStringLiteral(R"(
        * { outline: none; }
        QMainWindow { background: #09090b; }
        QWidget      { background: #e9e9e9; color: #09090b;
                        font-family: "Helvetica Neue", "Arial"; }
        QLabel  { background: transparent; border: none; }
        QFrame  { border: none; background: transparent; }
        QStackedWidget { background: transparent; }

        QPushButton {
            background: #18181b; color: #ffffff;
            border: none; border-radius: 0px;
            font-size: 16px; font-weight: 900; letter-spacing: 1px;
            padding: 0 20px;
        }
        QPushButton:hover   { background: #27272a; }
        QPushButton:pressed { background: #3f3f46; }

        QPushButton#btnDanger {
            background: #dc2626; color: #ffffff;
        }
        QPushButton#btnDanger:hover { background: #b91c1c; }

        QPushButton#btnSuccess {
            background: #059669; color: #ffffff;
        }
        QPushButton#btnSuccess:hover { background: #047857; }

        QPushButton#btnSupport {
            background: #dc2626; color: #ffffff;
            font-weight: 900; letter-spacing: 1px;
        }
    )");
}

inline QString cardStyle(const QString& bg = BG_WHITE,
                          const QString&   = HEAVY_BORDER,
                          int              = 0) {
    return QStringLiteral("background:%1;border:none;").arg(bg);
}

} // namespace theme
