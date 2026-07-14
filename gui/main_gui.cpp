// gui/main_gui — graficzna powloka predeye (Dear ImGui + GLFW + OpenGL3).
//
// KREATOR RANKEDOWY: lewy pasek prowadzi przez etapy meczu —
//   1. Bany  2. Wybor bohatera  3. Eternals + skill  4. Crest  5. Itemy (live)
// plus Kalibracja jako tryb zaawansowany. Nawigacja RECZNA (uzytkownik
// przelacza etapy), w kazdym etapie przycisk (i hotkey F9 na Windows) robi
// zrzut/odczyt na zyczenie. Kazdy przycisk ma opis co robi, a praca w tle
// (AsyncTask) pokazuje animowany spinner — petla renderowania nie zamarza.
//
// Kalibracja siatek scoreboardu startuje AUTOMATYCZNIE: pierwszy odczyt live
// bez pliku kalibracji robi auto-kalibracje (NCC wokol domyslnej siatki);
// pewny wynik zapisuje sie sam, niepewny odsyla do etapu Kalibracja.
//
// Zero ingerencji w gre (§3): jedyne wejscie wizualne to zrzut ekranu przez
// publiczne API systemu, prezentacja to wynik rdzenia.

#include "core/advisor.hpp"
#include "core/hero_context.hpp"
#include "gui/async_task.hpp"
#include "vision/calibration.hpp"
#include "vision/capture.hpp"
#include "vision/vision_session.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>
#ifdef _WIN32
// Natywny dialog wyboru pliku (GetOpenFileNameW) — bez nowych zaleznosci.
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>

#include <commdlg.h>

#include "vision/capture_dxgi.hpp"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace predeye;

// --- Role: etykieta widoczna w GUI + wartosc enuma ---------------------------
struct RoleChoice {
    const char* label;
    Role role;
};
constexpr std::array<RoleChoice, 5> kRoles = {{
    {"Midlane", Role::Midlane},
    {"Carry", Role::Carry},
    {"Offlane", Role::Offlane},
    {"Jungle", Role::Jungle},
    {"Support", Role::Support},
}};

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// --- Drobne widgety kreatora -------------------------------------------------

// Animowany spinner "program pracuje" (luk obracajacy sie wg czasu ImGui).
void draw_spinner(float radius = 8.0f) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 center(pos.x + radius, pos.y + radius + 2.0f);
    const float t = static_cast<float>(ImGui::GetTime());
    const float a0 = t * 6.0f;
    dl->PathArcTo(center, radius, a0, a0 + 4.7f, 24);
    dl->PathStroke(ImGui::GetColorU32(ImGuiCol_ButtonHovered), 0, 3.0f);
    ImGui::Dummy(ImVec2(radius * 2.0f + 4.0f, radius * 2.0f + 4.0f));
}

// Spinner + etykieta w tej samej linii (uzywac po przycisku etapu).
void spinner_with_label(const char* label) {
    ImGui::SameLine();
    draw_spinner();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", label);
}

// Znacznik "(?)" z tooltipem — opis co robi przycisk/sekcja.
void help_marker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Filtrowalna lista wyboru bohatera: pokazuje aktualny tekst w buf, po
// otwarciu daje pole filtra i przewijana liste pasujacych nazw. Puste
// `names` (dane jeszcze nie zaladowane) => zwykle pole tekstowe.
void hero_combo(const char* id, std::string& buf, const std::vector<std::string>& names) {
    if (names.empty()) {
        char tmp[64];
        std::snprintf(tmp, sizeof(tmp), "%s", buf.c_str());
        if (ImGui::InputText(id, tmp, sizeof(tmp)))
            buf = tmp;
        return;
    }
    static char filter[64] = "";
    if (ImGui::BeginCombo(id, buf.empty() ? "(wybierz bohatera)" : buf.c_str())) {
        ImGui::InputTextWithHint("##filter", "filtruj...", filter, sizeof(filter));
        const std::string needle = to_lower(filter);
        for (const auto& n : names) {
            if (!needle.empty() && to_lower(n).find(needle) == std::string::npos)
                continue;
            const bool selected = (n == buf);
            if (ImGui::Selectable(n.c_str(), selected)) {
                buf = n;
                filter[0] = '\0';
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void role_combo(const char* id, int& idx) {
    if (ImGui::BeginCombo(id, kRoles[static_cast<size_t>(idx)].label)) {
        for (int i = 0; i < static_cast<int>(kRoles.size()); ++i) {
            const bool selected = (i == idx);
            if (ImGui::Selectable(kRoles[static_cast<size_t>(i)].label, selected))
                idx = i;
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

// Tabela itemow buildu (nazwa, cena, ocena, slot, powod).
void draw_build_table(const char* id, const BuildResult& res) {
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable(id, 5, flags)) {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Cena", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Ocena", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthStretch, 0.9f);
        ImGui::TableSetupColumn("Powod", ImGuiTableColumnFlags_WidthStretch, 2.5f);
        ImGui::TableHeadersRow();
        for (const auto& p : res.items) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(p.item.display_name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%d zl", p.item.total_price);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.1f", p.score);
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(p.item.slot_type.c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(p.reason.c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextWrapped("%s", res.summary.c_str());
}

// Tabela jednej druzyny z odczytu live: rola+bohater | itemy | zmiana.
// Itemy rysowane osobno — najechanie pokazuje spolszczenie (tooltip_pl).
void draw_team_table(const char* id, const char* title, const std::vector<LiveRowView>& rows) {
    ImGui::SeparatorText(title);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, 3, flags))
        return;
    ImGui::TableSetupColumn("Rola / bohater", ImGuiTableColumnFlags_WidthStretch, 1.3f);
    ImGui::TableSetupColumn("Itemy (najedz = opis PL)", ImGuiTableColumnFlags_WidthStretch, 3.2f);
    ImGui::TableSetupColumn("Zmiana", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableHeadersRow();
    for (const auto& r : rows) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (r.hero_name.empty())
            ImGui::TextUnformatted(r.role_label.c_str());
        else
            ImGui::Text("%s (%s)", r.role_label.c_str(), r.hero_name.c_str());
        ImGui::TableSetColumnIndex(1);
        bool any = false;
        for (const auto& s : r.slots) {
            if (s.empty)
                continue;
            if (any)
                ImGui::SameLine(0.0f, 0.0f);
            std::string label = (any ? ", " : "") + s.name + (s.confident ? "" : " (niepewne)");
            ImGui::TextUnformatted(label.c_str());
            if (!s.tooltip_pl.empty() && ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
                ImGui::TextUnformatted(s.tooltip_pl.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            any = true;
        }
        if (!any)
            ImGui::TextDisabled("(pusto)");
        ImGui::TableSetColumnIndex(2);
        if (!r.changes.empty()) {
            std::string delta;
            for (const auto& ch : r.changes)
                delta += (delta.empty() ? "" : ", ") + ch;
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "%s", delta.c_str());
        }
    }
    ImGui::EndTable();
}

// --- Tekstura GL z klatki OpenCV (BGR) — podglad w oknie ImGui ---------------
// GL 1.1 (glGenTextures/glTexImage2D) dostepne przez naglowki GLFW; upload
// tylko z watku UI (kontekst GL). Zwalnia teksture w destruktorze.
struct GlTexture {
    GLuint id = 0;
    int w = 0, h = 0;

    void upload(const cv::Mat& bgr) {
        cv::Mat rgba;
        cv::cvtColor(bgr, rgba, cv::COLOR_BGR2RGBA);
        if (id == 0)
            glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba.cols, rgba.rows, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba.data);
        w = rgba.cols;
        h = rgba.rows;
    }

    ~GlTexture() {
        if (id != 0)
            glDeleteTextures(1, &id);
    }
};

// --- Wspolne zrodlo klatki (plik PNG albo zrzut z gry przez DXGI) ------------
// Wolane z watku tla. Pusta sciezka = zrzut z gry (tylko Windows).
cv::Mat grab_frame(const std::string& image_path) {
    if (!image_path.empty())
        return FileCapture(image_path).grab();
#ifdef _WIN32
    return DxgiCapture().grab();
#else
    throw std::runtime_error("podaj zrzut PNG — zrzut z gry dziala tylko na Windows");
#endif
}

// --- Stan aplikacji ----------------------------------------------------------

// Etapy kreatora (kolejnosc = przebieg meczu rankedowego).
enum Stage {
    kStageBans = 0,
    kStagePick,
    kStageEternals,
    kStageCrest,
    kStageItems,
    kStageCalib,
    kStageCount
};
constexpr const char* kStageNames[kStageCount] = {
    "1. Bany",         "2. Wybor bohatera", "3. Eternals + skill",
    "4. Crest",        "5. Itemy (live)",   "Kalibracja (zaawansowane)",
};

struct AppState {
    // Advisor: lista bohaterow + sugestie draftu/loadoutu (siec w tle).
    // Osobna instancja od VisionSession — watki nie dziela stanu.
    Advisor advisor;
    gui::AsyncTask<std::vector<std::string>> hero_names_task;
    std::vector<std::string> hero_names;

    // Wspolna sesja toru wizyjnego (dane API + bazy ikon + stan diffa).
    VisionSession vision;

    GLFWwindow* window = nullptr; // wlasciciel natywnego dialogu pliku
    int stage = kStageBans;
    bool f9_edge = false; // F9 wcisniete w tej klatce (Windows)

    // Wspolne ustawienia kreatora (naglowek).
    std::string hero; // moj bohater (etap 2 moze go ustawic)
    int role = 0;
    char config_path[512] = "calibration.json";
    char image_path[512] = ""; // puste = zrzut z gry (DXGI, Windows)

    // Etapy 1-2: odczyt draftu + sugestie banow/pickow.
    struct DraftStage {
        gui::AsyncTask<DraftRead> read_task;
        bool read_consumed = true;
        std::optional<DraftRead> last;
        gui::AsyncTask<AdvisorDraft> sugg_task;
        bool sugg_consumed = true;
        std::optional<AdvisorDraft> sugg;
        std::string status;
    } draft;

    // Etapy 3-4: crest + skill order + eternalsy dla (bohater, rola).
    struct LoadoutStage {
        gui::AsyncTask<LoadoutAdvice> task;
        bool consumed = true;
        std::optional<LoadoutAdvice> last;
        std::string applied_hero;
        int applied_role = -1;
    } loadout;

    // Etap 5: live (odczyt scoreboardu + counter + zakupy).
    struct LiveStage {
        std::string applied_hero;
        int applied_role = -1;
        gui::AsyncTask<LiveResult> read_task;
        bool read_consumed = true;
        std::optional<LiveResult> last;
        std::string status;
    } live;

    // Kalibracja (tryb zaawansowany).
    struct CalibTab {
        Calibration calib = Calibration::default_for({1920, 1080});
        cv::Mat frame; // wczytany zrzut (BGR)
        bool has_frame = false;
        bool dirty = true; // przelicz overlay + upload tekstury
        GlTexture tex;
        char image_path[512] = "";
        std::string status;
        int edit_grid = 0;                // indeks kSlotGrids
        std::vector<std::string> gallery; // obrazy w katalogu ostatniego zrzutu (UTF-8)
        int gallery_idx = -1;             // pozycja biezacego zrzutu w galerii
        gui::AsyncTask<cv::Mat> shot_task; // zrzut z gry (DXGI, z odliczaniem)
        bool shot_consumed = true;         // czy wynik zrzutu juz odebrano
    } calib;
};

// Ktora siatke edytuja suwaki kalibracji.
struct GridChoice {
    const char* label;
    GridSpec& (*get)(Calibration&);
};
constexpr std::array<GridChoice, 8> kSlotGrids = {{
    {"Itemy: wrogowie", [](Calibration& c) -> GridSpec& { return c.enemy_item_grid; }},
    {"Itemy: moja druzyna", [](Calibration& c) -> GridSpec& { return c.ally_item_grid; }},
    {"Portrety: wrogowie", [](Calibration& c) -> GridSpec& { return c.enemy_hero_grid; }},
    {"Portrety: moja druzyna", [](Calibration& c) -> GridSpec& { return c.ally_hero_grid; }},
    {"Draft: bany moje", [](Calibration& c) -> GridSpec& { return c.draft.ally_bans; }},
    {"Draft: bany wroga", [](Calibration& c) -> GridSpec& { return c.draft.enemy_bans; }},
    {"Draft: picki moje", [](Calibration& c) -> GridSpec& { return c.draft.ally_picks; }},
    {"Draft: picki wroga", [](Calibration& c) -> GridSpec& { return c.draft.enemy_picks; }},
}};

#ifdef _WIN32
// Natywny dialog "Otworz plik" (obrazy); zwraca sciezke UTF-8, pusta gdy
// anulowano. OFN_NOCHANGEDIR konieczny: dialog zmienia katalog roboczy,
// a wzgledne sciezki (calibration.json) musza pozostac stabilne.
std::string open_image_dialog(GLFWwindow* owner) {
    wchar_t buf[1024] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner ? glfwGetWin32Window(owner) : nullptr;
    ofn.lpstrFilter = L"Obrazy (*.png;*.jpg;*.jpeg;*.bmp)\0*.png;*.jpg;*.jpeg;*.bmp\0"
                      L"Wszystkie pliki (*.*)\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = static_cast<DWORD>(std::size(buf));
    ofn.lpstrTitle = L"Wybierz zrzut ekranu";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn))
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, buf, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1)
        return {};
    std::string utf8(static_cast<size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8.data(), n, nullptr, nullptr);
    return utf8;
}
#endif

// Wczytaj kalibracje z pliku, a gdy go brak — auto-kalibracja na klatce
// (zapis przy pewnym wyniku). Wspolna sciezka odczytow live. Watek tla.
Calibration load_or_autocalibrate(VisionSession& vs, const std::string& cfg,
                                  const cv::Mat& frame, std::string& note) {
    if (fs::exists(fs::u8path(cfg)))
        return Calibration::load(cfg);
    const AutoCalibResult ac = vs.auto_calibrate_frame(frame);
    if (!ac.ok())
        throw std::runtime_error(
            "brak " + cfg + ", a auto-kalibracja niepewna (cosine " +
            std::to_string(ac.enemy_confidence) +
            ") — na ekranie musza byc widoczne itemy; dostroj siatke w etapie Kalibracja");
    ac.calib.save(cfg);
    note = "Auto-kalibracja OK (cosine " + std::to_string(ac.enemy_confidence).substr(0, 4) +
           "), zapisano " + cfg + ".";
    return ac.calib;
}

// Galeria: pliki obrazow z katalogu wczytanego zrzutu (posortowane) + pozycja
// biezacego pliku. Sciezki trzymamy jako UTF-8 (zgodnie z FileCapture).
void calib_refresh_gallery(AppState::CalibTab& c, const std::string& current) {
    c.gallery.clear();
    c.gallery_idx = -1;
    std::error_code ec;
    const fs::path cur = fs::u8path(current);
    fs::path dir = cur.parent_path();
    if (dir.empty())
        dir = fs::current_path(ec);
    if (ec || !fs::is_directory(dir, ec) || ec)
        return;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec))
            continue;
        const std::string ext = to_lower(it->path().extension().u8string());
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
            c.gallery.push_back(it->path().u8string());
    }
    std::sort(c.gallery.begin(), c.gallery.end());
    const std::string name = cur.filename().u8string();
    for (size_t i = 0; i < c.gallery.size(); ++i)
        if (fs::u8path(c.gallery[i]).filename().u8string() == name)
            c.gallery_idx = static_cast<int>(i);
}

// Wczytaj klatke z pliku do zakladki kalibracji (synchronicznie — lokalne)
// i odswiez galerie katalogu.
void calib_load_image(AppState::CalibTab& c, const std::string& path) {
    try {
        c.frame = FileCapture(path).grab();
        c.has_frame = true;
        c.dirty = true;
        std::snprintf(c.image_path, sizeof(c.image_path), "%s", path.c_str());
        // Startowa siatka dla tej rozdzielczosci (chyba ze wczytano juz JSON).
        if (c.calib.enemy_item_grid.cols == 0 || c.calib.resolution != c.frame.size())
            c.calib = Calibration::default_for(c.frame.size());
        c.status = "Wczytano zrzut " + std::to_string(c.frame.cols) + "x" +
                   std::to_string(c.frame.rows) + ".";
        calib_refresh_gallery(c, path);
    } catch (const std::exception& ex) {
        c.status = std::string("Blad wczytania: ") + ex.what();
    }
}

// --- Etapy 1-2: draft ---------------------------------------------------------

// Start odczytu ekranu draftu + (po nim) sugestii. Wspolny dla etapow 1 i 2.
void start_draft_read(AppState& s) {
    VisionSession* vs = &s.vision;
    const std::string cfg = s.config_path;
    const std::string img = s.image_path;
    s.draft.read_consumed = false;
    s.draft.status.clear();
    s.draft.read_task.start([vs, cfg, img]() -> DraftRead {
        const cv::Mat frame = grab_frame(img);
        Calibration calib = fs::exists(fs::u8path(cfg)) ? Calibration::load(cfg)
                                                        : Calibration::default_for(frame.size());
        return vs->read_draft_frame(frame, calib);
    });
}

// Po odczycie draftu: policz sugestie (bany + picki) w tle.
void start_draft_suggestions(AppState& s) {
    Advisor* adv = &s.advisor;
    std::vector<std::string> enemy_picks, excluded;
    if (s.draft.last) {
        enemy_picks = DraftRead::names(s.draft.last->enemy_picks);
        for (const auto& list : {s.draft.last->ally_bans, s.draft.last->enemy_bans,
                                 s.draft.last->ally_picks, s.draft.last->enemy_picks})
            for (const auto& n : DraftRead::names(list))
                excluded.push_back(n);
    }
    s.draft.sugg_consumed = false;
    s.draft.sugg_task.start(
        [adv, enemy_picks, excluded] { return adv->draft(enemy_picks, excluded); });
}

// Przycisk odczytu wspolny dla etapow draftu; `via_f9` = wyzwolony hotkeyem.
void draft_read_controls(AppState& s, bool via_f9) {
    const bool busy = s.draft.read_task.running() || s.draft.sugg_task.running();
    ImGui::BeginDisabled(busy);
    const bool clicked = ImGui::Button("Odczytaj ekran draftu (F9)", ImVec2(240, 0));
    ImGui::EndDisabled();
    help_marker("Robi zrzut ekranu (lub czyta podany PNG), rozpoznaje portrety zbanowanych "
                "i wybranych bohaterow z regionow draftu w kalibracji, a potem liczy sugestie "
                "banow/pickow (meta z Omeda.city + Twoja baza kontr data/counters.json). "
                "Wymaga skalibrowanych regionow draftu (etap Kalibracja) — potrzebny zrzut "
                "ekranu draftu.");
    if ((clicked || (via_f9 && !busy)))
        start_draft_read(s);
    if (s.draft.read_task.running())
        spinner_with_label("odczyt ekranu draftu...");
    if (s.draft.sugg_task.running())
        spinner_with_label("licze sugestie (meta + kontry)...");

    // Sugestie mozna policzyc tez bez odczytu ekranu (draft "w glowie").
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Same sugestie##draft"))
        start_draft_suggestions(s);
    ImGui::EndDisabled();
    help_marker("Liczy sugestie bez czytania ekranu (przydatne, gdy regiony draftu nie sa "
                "jeszcze skalibrowane). Bany: najgrozniejsi z mety; picki: Twoja pula "
                "(data/hero_pool.json) bez kontekstu pickow wroga.");

    if (!s.draft.read_task.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Blad odczytu: %s",
                           s.draft.read_task.error().c_str());
    if (!s.draft.sugg_task.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Blad sugestii: %s",
                           s.draft.sugg_task.error().c_str());
    if (!s.draft.status.empty())
        ImGui::TextWrapped("%s", s.draft.status.c_str());

    if (s.draft.last && !s.draft.last->calibrated)
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                           "Regiony draftu nieskalibrowane — ustaw siatki \"Draft: ...\" "
                           "w etapie Kalibracja na zrzucie ekranu draftu.");
}

// Lista rozpoznanych slotow draftu w jednej linii.
std::string draft_line(const std::vector<DraftSlot>& slots) {
    std::string out;
    for (const auto& sl : slots) {
        if (sl.empty)
            continue;
        out += (out.empty() ? "" : ", ") + (sl.hero_name.empty() ? "?" : sl.hero_name);
        if (!sl.confident)
            out += " (niepewne)";
    }
    return out.empty() ? "(nic nie rozpoznano)" : out;
}

void draw_suggestions_table(const char* id, const std::vector<DraftSuggestion>& sugg) {
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, 2, flags))
        return;
    ImGui::TableSetupColumn("Bohater", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Dlaczego", ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableHeadersRow();
    for (const auto& b : sugg) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(b.hero.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextWrapped("%s", b.reason_pl.c_str());
    }
    ImGui::EndTable();
}

void draw_bans_stage(AppState& s) {
    ImGui::TextDisabled("Faza banow: odczytaj ekran draftu, a program zaproponuje bany — "
                        "najgrozniejsi bohaterowie z mety + kontry na Twoja pule.");
    ImGui::Spacing();
    draft_read_controls(s, s.f9_edge);
    ImGui::Spacing();

    if (s.draft.last && s.draft.last->calibrated) {
        ImGui::SeparatorText("Odczyt draftu");
        ImGui::Text("Bany wroga: %s", draft_line(s.draft.last->enemy_bans).c_str());
        ImGui::Text("Bany moje:  %s", draft_line(s.draft.last->ally_bans).c_str());
    }
    if (s.draft.sugg) {
        ImGui::SeparatorText("Sugestie banow");
        draw_suggestions_table("bans_sugg", s.draft.sugg->bans);
    }
}

void draw_pick_stage(AppState& s) {
    ImGui::TextDisabled("Faza pickow: odczytaj drafta, a program oceni bohaterow z Twojej puli "
                        "przeciw widocznym pickom wroga (kontry + meta + bilans obrazen).");
    ImGui::Spacing();
    draft_read_controls(s, s.f9_edge);
    ImGui::Spacing();

    if (s.draft.last && s.draft.last->calibrated) {
        ImGui::SeparatorText("Odczyt draftu");
        ImGui::Text("Picki wroga: %s", draft_line(s.draft.last->enemy_picks).c_str());
        ImGui::Text("Picki moje:  %s", draft_line(s.draft.last->ally_picks).c_str());
    }
    if (s.draft.sugg) {
        ImGui::SeparatorText("Sugestie pickow (Twoja pula: data/hero_pool.json)");
        if (s.draft.sugg->picks.empty())
            ImGui::TextWrapped("Brak kandydatow — uzupelnij data/hero_pool.json.");
        draw_suggestions_table("picks_sugg", s.draft.sugg->picks);
        // Klik przejmuje picka do naglowka (etapy 3-5 licza dla niego).
        for (const auto& p : s.draft.sugg->picks) {
            ImGui::PushID(p.hero.c_str());
            if (ImGui::SmallButton(("Gram " + p.hero).c_str()))
                s.hero = p.hero;
            ImGui::PopID();
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
}

// --- Etapy 3-4: loadout (eternals + skill, crest) ------------------------------

void loadout_controls(AppState& s) {
    auto& lo = s.loadout;
    const bool busy = lo.task.running();
    const bool changed = (s.hero != lo.applied_hero || s.role != lo.applied_role);
    ImGui::BeginDisabled(busy || s.hero.empty());
    const bool clicked = ImGui::Button("Pobierz rekomendacje", ImVec2(240, 0));
    ImGui::EndDisabled();
    help_marker("Pobiera najlepszy build spolecznosci dla (bohater, rola) z Omeda.city "
                "(crest + kolejnosc skillowania) i dokleja eternalsy z Twojej bazy "
                "data/eternals.json. Bohatera i role ustawiasz w naglowku.");
    if (clicked || (!busy && changed && lo.last)) {
        Advisor* adv = &s.advisor;
        const std::string hero = s.hero;
        const Role role = kRoles[static_cast<size_t>(s.role)].role;
        lo.applied_hero = s.hero;
        lo.applied_role = s.role;
        lo.consumed = false;
        lo.task.start([adv, hero, role] { return adv->loadout(hero, role); });
    }
    if (busy)
        spinner_with_label("pobieram build spolecznosci...");
    if (!lo.task.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Blad: %s", lo.task.error().c_str());
    if (s.hero.empty())
        ImGui::TextDisabled("Najpierw wybierz bohatera w naglowku (albo w etapie 2).");
}

void draw_eternals_stage(AppState& s) {
    ImGui::TextDisabled("Eternalsy i kolejnosc skillowania dla Twojego picka. Eternalsy "
                        "pochodza z Twojej bazy data/eternals.json (API ich nie ma), "
                        "skill order z najlepszego buildu spolecznosci.");
    ImGui::Spacing();
    loadout_controls(s);
    if (!s.loadout.last)
        return;
    const LoadoutAdvice& lo = *s.loadout.last;

    ImGui::SeparatorText("Eternalsy");
    if (lo.eternals.empty()) {
        ImGui::TextWrapped("Brak rekomendacji dla (%s, %s) w data/eternals.json — uzupelnij "
                           "baze (ikony + opisy z gry/pred.gg).",
                           lo.hero_name.c_str(),
                           kRoles[static_cast<size_t>(s.role)].label);
    }
    for (const auto* e : lo.eternals) {
        ImGui::BulletText("%s [%s]", e->name.c_str(), e->slot.c_str());
        if (!e->description_pl.empty()) {
            ImGui::Indent();
            ImGui::TextWrapped("%s", e->description_pl.c_str());
            ImGui::Unindent();
        }
    }

    ImGui::SeparatorText("Kolejnosc skillowania");
    if (lo.skill_order.empty()) {
        ImGui::TextDisabled("Build spolecznosci nie zawiera skill order.");
        return;
    }
    if (!lo.build_title.empty())
        ImGui::TextDisabled("wg buildu: \"%s\"", lo.build_title.c_str());
    const int levels = static_cast<int>(lo.skill_order.size());
    if (ImGui::BeginTable("skill_order", levels + 1,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_ScrollX)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Poziom");
        for (int i = 0; i < levels; ++i) {
            ImGui::TableSetColumnIndex(i + 1);
            ImGui::Text("%d", i + 1);
        }
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Skill");
        // Umiejetnosci numerowane 1..4 jak w rekordzie buildu (Q/E/RMB/ULT
        // zaleznie od bohatera — numer slotu, nie klawisza).
        for (int i = 0; i < levels; ++i) {
            ImGui::TableSetColumnIndex(i + 1);
            ImGui::Text("%d", lo.skill_order[static_cast<size_t>(i)]);
        }
        ImGui::EndTable();
    }
}

void draw_crest_stage(AppState& s) {
    ImGui::TextDisabled("Crest dla Twojego picka — z najlepszego buildu spolecznosci "
                        "(najwiecej glosow na Omeda.city), ze spolszczeniem dzialania.");
    ImGui::Spacing();
    loadout_controls(s);
    if (!s.loadout.last)
        return;
    const LoadoutAdvice& lo = *s.loadout.last;

    ImGui::SeparatorText("Crest");
    if (!lo.crest) {
        ImGui::TextWrapped("Build spolecznosci nie wskazuje crestu (albo brak buildow dla "
                           "tego bohatera).");
        return;
    }
    ImGui::Text("%s", lo.crest->display_name.c_str());
    if (!lo.build_title.empty())
        ImGui::TextDisabled("wg buildu: \"%s\"", lo.build_title.c_str());
    if (!lo.crest_note_pl.empty())
        ImGui::TextWrapped("%s", lo.crest_note_pl.c_str());
}

// --- Etap 5: itemy (live) -------------------------------------------------------

void start_live_read(AppState& s) {
    auto& lv = s.live;
    VisionSession* vs = &s.vision;
    const std::string hero = s.hero;
    const Role role = kRoles[static_cast<size_t>(s.role)].role;
    const std::string cfg = s.config_path;
    const std::string img = s.image_path;
    // Ustaw cel tylko przy zmianie (set_objective resetuje diff).
    const bool set_obj = (hero != lv.applied_hero || s.role != lv.applied_role);
    lv.applied_hero = hero;
    lv.applied_role = s.role;
    lv.read_consumed = false;
    lv.status.clear();
    lv.read_task.start([vs, hero, role, cfg, img, set_obj]() -> LiveResult {
        if (set_obj)
            vs->set_objective(hero, role);
        const cv::Mat frame = grab_frame(img);
        std::string note;
        const Calibration calib = load_or_autocalibrate(*vs, cfg, frame, note);
        LiveResult r = vs->read(frame, calib);
        if (!note.empty())
            r.shopping_note = note + " " + r.shopping_note;
        return r;
    });
}

void draw_items_stage(AppState& s) {
    auto& lv = s.live;
    ImGui::TextDisabled("Mecz: przytrzymaj TAB (scoreboard) i odczytaj — program rozpozna "
                        "itemy i bohaterow obu druzyn, doostrzy profil wroga, policzy "
                        "counter-build i podpowie NASTEPNY ZAKUP z uzasadnieniem.");
    ImGui::Spacing();

    const bool busy = lv.read_task.running();
#ifndef _WIN32
    const bool can_read = !s.hero.empty() && s.image_path[0] != '\0';
#else
    const bool can_read = !s.hero.empty();
#endif
    ImGui::BeginDisabled(busy || !can_read);
    const bool clicked = ImGui::Button("Odczytaj scoreboard (F9)", ImVec2(240, 0));
    ImGui::EndDisabled();
    help_marker("Robi zrzut ekranu (przytrzymaj TAB w grze!) albo czyta podany PNG. "
                "Rozpoznaje itemy i portrety bohaterow metoda NCC, liczy profil wroga "
                "i counter-build. Pierwszy odczyt bez pliku kalibracji uruchamia "
                "AUTO-KALIBRACJE (potrzebne widoczne itemy); pierwszy odczyt w ogole "
                "pobiera tez baze ikon (moze potrwac). Najechanie na item = opis PL.");
    if ((clicked || (s.f9_edge && !busy && can_read)))
        start_live_read(s);
    if (busy)
        spinner_with_label("odczyt / pobieranie bazy ikon...");
    if (s.vision.matcher_ready()) {
        ImGui::SameLine();
        ImGui::TextDisabled("baza: %d ikon", static_cast<int>(s.vision.icon_base_size()));
    }
    if (s.hero.empty())
        ImGui::TextDisabled("Najpierw wybierz bohatera w naglowku (albo w etapie 2).");
#ifndef _WIN32
    if (s.image_path[0] == '\0')
        ImGui::TextDisabled("Poza Windows podaj zrzut PNG w naglowku (brak DXGI).");
#endif

    if (!lv.read_task.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Blad: %s",
                           lv.read_task.error().c_str());
    if (!lv.last)
        return;

    const LiveResult& r = *lv.last;
    ImGui::SeparatorText("Nastepny zakup");
    if (!r.shopping_note.empty())
        ImGui::TextDisabled("%s", r.shopping_note.c_str());
    if (r.shopping.queue.empty()) {
        ImGui::TextWrapped("Masz komplet itemow z planu counter-builda.");
    } else {
        const NextPurchase& np = r.shopping.queue.front();
        ImGui::Text("KUP TERAZ: %s (%d zl)", np.item.display_name.c_str(),
                    np.item.total_price);
        ImGui::TextWrapped("Dlaczego: %s", np.reason_pl.c_str());
        if (r.shopping.queue.size() > 1) {
            std::string rest;
            for (size_t i = 1; i < r.shopping.queue.size(); ++i)
                rest += (rest.empty() ? "" : " -> ") + r.shopping.queue[i].item.display_name;
            ImGui::TextDisabled("Potem: %s (razem %d zl)", rest.c_str(),
                                r.shopping.remaining_cost);
        }
    }

    ImGui::SeparatorText("Odczyt");
    ImGui::Text("%d itemow (%d niepewnych)", r.total_items, r.uncertain);
    ImGui::TextDisabled("Ta sama rola w obu tabelach = przeciwnicy w linii "
                        "(scoreboard sortuje graczy wg rol).");
    draw_team_table("live_enemies", "Wrogowie", r.enemies);
    draw_team_table("live_allies", "Moja druzyna", r.allies);

    ImGui::Text("Profil wroga: %.0f%% obrazen fizycznych%s%s%s", 100.0 * r.profile.physical_ratio,
                r.profile.has_healing ? ", leczenie" : "", r.profile.has_crit ? ", kryt" : "",
                r.profile.has_tanks ? ", tanki" : "");

    ImGui::SeparatorText("Counter-build (pelny cel)");
    draw_build_table("live_counter", r.counter);
}

// --- Kalibracja (zaawansowane) -------------------------------------------------

void draw_calibrate_stage(AppState& s) {
    auto& c = s.calib;
    ImGui::TextDisabled("Tryb zaawansowany: reczne dopasowanie siatek do zrzutow. Zwykle "
                        "NIEPOTRZEBNE dla scoreboardu (etap 5 auto-kalibruje) — konieczne "
                        "dla regionow draftu (bany/picki) na zrzucie ekranu draftu.");
    ImGui::Spacing();

    // --- Zrodlo klatki ---
    ImGui::InputText("Zrzut PNG##calib", c.image_path, sizeof(c.image_path));
    ImGui::SameLine();
    if (ImGui::Button("Wczytaj##calibimg") && c.image_path[0] != '\0')
        calib_load_image(c, c.image_path);
#ifdef _WIN32
    ImGui::SameLine();
    if (ImGui::Button("Przegladaj...##calibbrowse")) {
        const std::string picked = open_image_dialog(s.window);
        if (!picked.empty())
            calib_load_image(c, picked);
    }
    ImGui::SameLine();
    const bool shooting = c.shot_task.running();
    ImGui::BeginDisabled(shooting);
    if (ImGui::Button("Zrzut z gry (3 s)##calibshot")) {
        c.status = "Przelacz sie do gry (TAB dla scoreboardu / ekran draftu) — zrzut za 3 s...";
        c.shot_consumed = false;
        c.shot_task.start([] {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return DxgiCapture().grab();
        });
    }
    ImGui::EndDisabled();
    if (shooting)
        spinner_with_label("odliczanie do zrzutu...");
#endif
    ImGui::TextDisabled("Mozesz tez przeciagnac plik obrazu na okno.");

    // --- Galeria screenow z katalogu ostatnio wczytanego zrzutu ---
    if (!c.gallery.empty()) {
        ImGui::BeginDisabled(c.gallery_idx <= 0);
        if (ImGui::ArrowButton("##galprev", ImGuiDir_Left))
            calib_load_image(c, c.gallery[static_cast<size_t>(c.gallery_idx - 1)]);
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(c.gallery_idx < 0 ||
                             c.gallery_idx + 1 >= static_cast<int>(c.gallery.size()));
        if (ImGui::ArrowButton("##galnext", ImGuiDir_Right))
            calib_load_image(c, c.gallery[static_cast<size_t>(c.gallery_idx + 1)]);
        ImGui::EndDisabled();
        ImGui::SameLine();
        const std::string current =
            c.gallery_idx >= 0
                ? fs::u8path(c.gallery[static_cast<size_t>(c.gallery_idx)]).filename().u8string()
                : std::string("(wybierz)");
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0f);
        if (ImGui::BeginCombo("Galeria##calibgal", current.c_str())) {
            for (size_t i = 0; i < c.gallery.size(); ++i) {
                const std::string name = fs::u8path(c.gallery[i]).filename().u8string();
                if (ImGui::Selectable(name.c_str(), static_cast<int>(i) == c.gallery_idx))
                    calib_load_image(c, c.gallery[i]);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%d/%d", c.gallery_idx + 1, static_cast<int>(c.gallery.size()));
    }

    // --- Plik kalibracji (wspolna sciezka w naglowku) ---
    if (ImGui::Button("Wczytaj JSON##calibcfg")) {
        try {
            c.calib = Calibration::load(s.config_path);
            c.dirty = true;
            c.status = std::string("Wczytano ") + s.config_path + ".";
        } catch (const std::exception& ex) {
            c.status = std::string("Blad JSON: ") + ex.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Zapisz JSON##calibcfg")) {
        try {
            c.calib.save(s.config_path);
            c.status = std::string("Zapisano ") + s.config_path + ".";
        } catch (const std::exception& ex) {
            c.status = std::string("Blad zapisu: ") + ex.what();
        }
    }
    help_marker("Plik kalibracji ustawiasz w naglowku (wspolny dla wszystkich etapow).");
    if (c.has_frame) {
        ImGui::SameLine();
        if (ImGui::Button("Domyslne dla rozdzielczosci##calib")) {
            c.calib = Calibration::default_for(c.frame.size());
            c.dirty = true;
        }
    }

    if (!c.status.empty())
        ImGui::TextWrapped("%s", c.status.c_str());
    ImGui::Separator();

    // --- Wybor edytowanej siatki + suwaki ---
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    if (ImGui::BeginCombo("Edytowana siatka", kSlotGrids[static_cast<size_t>(c.edit_grid)].label)) {
        for (int i = 0; i < static_cast<int>(kSlotGrids.size()); ++i) {
            if (ImGui::Selectable(kSlotGrids[static_cast<size_t>(i)].label, i == c.edit_grid))
                c.edit_grid = i;
        }
        ImGui::EndCombo();
    }
    help_marker("Siatki draftu (bany/picki) startuja jako nieobecne (0 wierszy) — ustaw "
                "wiersze/kolumny > 0 na zrzucie ekranu draftu, aby etapy 1-2 mogly czytac "
                "bany i picki. Klik na obrazie ustawia origin edytowanej siatki.");

    auto& g = kSlotGrids[static_cast<size_t>(c.edit_grid)].get(c.calib);
    const int max_x = c.has_frame ? c.frame.cols : 4096;
    const int max_y = c.has_frame ? c.frame.rows : 4096;
    bool changed = false;
    changed |= ImGui::DragInt("origin X", &g.origin.x, 1.0f, 0, max_x);
    changed |= ImGui::DragInt("origin Y", &g.origin.y, 1.0f, 0, max_y);
    changed |= ImGui::DragInt("slot szer.", &g.slot.width, 1.0f, 4, 256);
    changed |= ImGui::DragInt("slot wys.", &g.slot.height, 1.0f, 4, 256);
    changed |= ImGui::DragInt("dx (krok w wierszu)", &g.dx, 1.0f, 0, max_x);
    changed |= ImGui::DragInt("dy (krok wierszy)", &g.dy, 1.0f, 0, max_y);
    changed |= ImGui::DragInt("kolumny", &g.cols, 0.1f, 0, 16);
    changed |= ImGui::DragInt("wiersze", &g.rows, 0.1f, 0, 8);
    if (changed)
        c.dirty = true;

    if (!c.has_frame) {
        ImGui::Spacing();
        ImGui::TextDisabled("Wczytaj zrzut PNG (lub zrob zrzut z gry), aby zobaczyc podglad.");
        return;
    }

    // --- Podglad z siatkami ---
    if (c.dirty) {
        c.tex.upload(draw_grid(c.frame, c.calib));
        c.dirty = false;
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const float scale = (c.tex.w > 0) ? std::min(1.0f, avail / static_cast<float>(c.tex.w)) : 1.0f;
    const ImVec2 disp(c.tex.w * scale, c.tex.h * scale);
    ImGui::Image((ImTextureID)(intptr_t)c.tex.id, disp);

    // Klik na obrazie -> origin edytowanej siatki (piksele pelnej rozdzielczosci).
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && scale > 0.0f) {
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        const int px = static_cast<int>((mouse.x - p0.x) / scale);
        const int py = static_cast<int>((mouse.y - p0.y) / scale);
        g.origin.x = std::clamp(px, 0, c.frame.cols);
        g.origin.y = std::clamp(py, 0, c.frame.rows);
        c.dirty = true;
    }
}

// --- Uklad glowny ----------------------------------------------------------------

// Naglowek kreatora: bohater, rola, zrodlo klatki, plik kalibracji, statusy.
void draw_header(AppState& s) {
    ImGui::TextUnformatted("predeye - asystent rankedowy Predecessor");
    ImGui::SameLine();
    if (s.hero_names_task.running()) {
        draw_spinner(6.0f);
        ImGui::SameLine();
        ImGui::TextDisabled("(laduje liste bohaterow...)");
    } else if (s.hero_names.empty() && !s.hero_names_task.error().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "(brak listy bohaterow: %s - mozna wpisac nazwe recznie)",
                           s.hero_names_task.error().c_str());
    } else if (!s.hero_names.empty()) {
        ImGui::TextDisabled("(%d bohaterow)", static_cast<int>(s.hero_names.size()));
    }

    // Status recznych danych z data/ — kreator na nich polega.
    const LocalData& ld = s.advisor.local();
    ImGui::SameLine();
    ImGui::TextDisabled("| dane: %d kontr, %d bohaterow w puli, %d spolszczen, %d eternalsow",
                        ld.counters_count(), static_cast<int>(ld.hero_pool().heroes.size()),
                        ld.items_pl_count(), static_cast<int>(ld.eternals().size()));
    help_marker("Reczne dane z katalogu data/ w repo: counters.json (kontry), "
                "hero_pool.json (Twoja pula), pl_items.json (spolszczenia), "
                "eternals.json (eternalsy). Uzupelniaj je — sugestie beda trafniejsze.");

    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
    hero_combo("Moj bohater##hdr", s.hero, s.hero_names);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
    role_combo("Rola##hdr", s.role);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
    ImGui::InputText("Kalibracja##hdr", s.config_path, sizeof(s.config_path));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.0f);
#ifdef _WIN32
    ImGui::InputTextWithHint("Zrzut##hdr", "(puste = zrzut z gry)", s.image_path,
                             sizeof(s.image_path));
#else
    ImGui::InputTextWithHint("Zrzut##hdr", "sciezka PNG (wymagana)", s.image_path,
                             sizeof(s.image_path));
#endif
    help_marker("Wspolne ustawienia wszystkich etapow. Bohater/rola = Twoj pick; plik "
                "kalibracji tworzony auto-kalibracja przy pierwszym odczycie; pole Zrzut "
                "pozwala czytac z pliku PNG zamiast ekranu (test bez gry).");
    ImGui::Separator();
}

void draw_main_window(AppState& s) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("predeye", nullptr, flags);
    draw_header(s);

    // Lewy pasek: etapy kreatora (nawigacja reczna).
    ImGui::BeginChild("stages", ImVec2(ImGui::GetFontSize() * 13.0f, 0),
                      ImGuiChildFlags_Borders);
    for (int i = 0; i < kStageCount; ++i) {
        if (ImGui::Selectable(kStageNames[i], s.stage == i))
            s.stage = i;
    }
    ImGui::Separator();
    ImGui::TextDisabled("F9 = odczyt ekranu\nw biezacym etapie\n(Windows)");
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("stage_content", ImVec2(0, 0));
    switch (s.stage) {
    case kStageBans:
        draw_bans_stage(s);
        break;
    case kStagePick:
        draw_pick_stage(s);
        break;
    case kStageEternals:
        draw_eternals_stage(s);
        break;
    case kStageCrest:
        draw_crest_stage(s);
        break;
    case kStageItems:
        draw_items_stage(s);
        break;
    default:
        draw_calibrate_stage(s);
        break;
    }
    ImGui::EndChild();
    ImGui::End();
}

// Przeciagniecie pliku na okno -> wczytaj do etapu Kalibracja.
// GLFW dostarcza sciezki w UTF-8 (spojnie z FileCapture).
void drop_callback(GLFWwindow* window, int count, const char** paths) {
    if (count <= 0)
        return;
    auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!s)
        return;
    calib_load_image(s->calib, paths[0]);
    s->stage = kStageCalib;
}

void glfw_error_callback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

} // namespace

int main() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::fprintf(stderr, "predeye-gui: nie udalo sie zainicjalizowac GLFW\n");
        return 1;
    }

    // OpenGL 3.0 + GLSL 130 (przenosne na Windows/Linux). macOS wymaga 3.2 core.
#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1100, 760, "predeye", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "predeye-gui: nie udalo sie utworzyc okna\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr; // bez pliku imgui.ini w katalogu roboczym
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    AppState state;
    state.window = window;
    glfwSetWindowUserPointer(window, &state);
    glfwSetDropCallback(window, drop_callback); // backend ImGui nie uzywa drop
    // Start: pobierz liste bohaterow w tle (rozgrzewa tez cache itemow).
    {
        Advisor* adv = &state.advisor;
        state.hero_names_task.start([adv] { return adv->hero_names(); });
    }

#ifdef _WIN32
    bool f9_down_prev = false;
#endif

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

#ifdef _WIN32
        // Globalny hotkey F9 (dziala, gdy fokus ma gra): zbocze narastajace
        // wyzwala odczyt w biezacym etapie kreatora.
        const bool f9_down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
        state.f9_edge = f9_down && !f9_down_prev;
        f9_down_prev = f9_down;
#else
        state.f9_edge = false;
#endif

        // Odbior wynikow z watkow tla.
        state.hero_names_task.poll();
        if (state.hero_names.empty() && state.hero_names_task.has_result())
            state.hero_names = state.hero_names_task.result();
        state.live.read_task.poll();
        if (state.live.read_task.has_result() && !state.live.read_consumed) {
            state.live.read_consumed = true;
            state.live.last = state.live.read_task.result();
        }
        state.draft.read_task.poll();
        if (state.draft.read_task.has_result() && !state.draft.read_consumed) {
            state.draft.read_consumed = true;
            state.draft.last = state.draft.read_task.result();
            // Po odczycie draftu od razu policz sugestie w tle.
            start_draft_suggestions(state);
        }
        state.draft.sugg_task.poll();
        if (state.draft.sugg_task.has_result() && !state.draft.sugg_consumed) {
            state.draft.sugg_consumed = true;
            state.draft.sugg = state.draft.sugg_task.result();
        }
        state.loadout.task.poll();
        if (state.loadout.task.has_result() && !state.loadout.consumed) {
            state.loadout.consumed = true;
            state.loadout.last = state.loadout.task.result();
        }
        state.calib.shot_task.poll();
        if (state.calib.shot_task.has_result() && !state.calib.shot_consumed) {
            // Odbior swiezego zrzutu z gry (upload tekstury nastapi w draw).
            state.calib.shot_consumed = true;
            state.calib.frame = state.calib.shot_task.result();
            state.calib.has_frame = true;
            state.calib.dirty = true;
            if (state.calib.calib.resolution != state.calib.frame.size())
                state.calib.calib = Calibration::default_for(state.calib.frame.size());
            state.calib.status = "Zrzut z gry gotowy.";
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        draw_main_window(state);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.09f, 0.10f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
