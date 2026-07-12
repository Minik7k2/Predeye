// gui/main_gui — graficzna powloka predeye (Dear ImGui + GLFW + OpenGL3).
//
// Zakladki: Build (bohater + rola), Counter (bohater + rola + do 5 wrogow),
// oraz — gdy zbudowane z torem wizyjnym (PREDEYE_HAS_VISION) — Kalibracja
// (siatka slotow scoreboardu na podgladzie zrzutu) i Live (odczyt itemow
// wroga z ekranu -> profil -> counter-build z diffem). Wszystkie wywolania
// API/wizji ida w tle (AsyncTask), by petla renderowania nie zamarzala.
//
// Zero ingerencji w gre (§3): jedyne wejscie wizualne to zrzut ekranu przez
// publiczne API systemu, prezentacja to wynik rdzenia.

#include "core/advisor.hpp"
#include "core/hero_context.hpp"
#include "gui/async_task.hpp"

#ifdef PREDEYE_HAS_VISION
#include "vision/calibration.hpp"
#include "vision/capture.hpp"
#include "vision/vision_session.hpp"
#ifdef _WIN32
#include "vision/capture_dxgi.hpp"
#endif
#include <chrono>
#include <filesystem>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>
#include <thread>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

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

#ifdef PREDEYE_HAS_VISION
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
#endif // PREDEYE_HAS_VISION

// --- Stan aplikacji ----------------------------------------------------------
struct AppState {
    Advisor advisor;

    // Lista nazw bohaterow (do combo) — ladowana w tle na starcie.
    gui::AsyncTask<std::vector<std::string>> hero_names_task;
    std::vector<std::string> hero_names;

    // Zakladka Build.
    std::string build_hero;
    int build_role = 0;
    gui::AsyncTask<AdvisorBuild> build_task;

    // Zakladka Counter.
    std::string counter_hero;
    int counter_role = 0;
    std::array<std::string, 5> enemies;
    gui::AsyncTask<AdvisorCounter> counter_task;

#ifdef PREDEYE_HAS_VISION
    // Wspolna sesja toru wizyjnego (dane API + baza ikon + stan diffa).
    VisionSession vision;

    // Zakladka Kalibracja.
    struct CalibTab {
        Calibration calib;
        cv::Mat frame; // wczytany zrzut (BGR)
        bool has_frame = false;
        bool dirty = true; // przelicz overlay + upload tekstury
        GlTexture tex;
        char image_path[256] = "";
        char config_path[256] = "calibration.json";
        std::string status;
        gui::AsyncTask<cv::Mat> shot_task; // zrzut z gry (DXGI, z odliczaniem)
        bool shot_consumed = true;         // czy wynik zrzutu juz odebrano
    } calib;

    // Zakladka Live.
    struct LiveTab {
        std::string hero;
        int role = 0;
        char config_path[256] = "calibration.json";
        char image_path[256] = "";
        std::string applied_hero; // ostatnio ustawiony cel (do wykrycia zmiany)
        int applied_role = -1;
        gui::AsyncTask<LiveResult> read_task;
        bool read_consumed = true; // czy wynik odczytu juz odebrano
        std::optional<LiveResult> last;
        std::string status;
    } live;
#endif
};

void draw_build_tab(AppState& s) {
    ImGui::TextDisabled("Build pod cel: dobierz itemy dla (bohater + rola).");
    ImGui::Spacing();
    hero_combo("Bohater##build", s.build_hero, s.hero_names);
    role_combo("Rola##build", s.build_role);
    ImGui::Spacing();

    const bool busy = s.build_task.running();
    ImGui::BeginDisabled(busy || s.build_hero.empty());
    if (ImGui::Button("Generuj build", ImVec2(180, 0))) {
        std::string hero = s.build_hero;
        Role role = kRoles[static_cast<size_t>(s.build_role)].role;
        Advisor* adv = &s.advisor;
        s.build_task.start([adv, hero, role] { return adv->build(hero, role); });
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("liczenie...");
    }

    if (!s.build_task.error().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Blad: %s",
                           s.build_task.error().c_str());
    } else if (s.build_task.has_result()) {
        const auto& r = s.build_task.result();
        ImGui::SeparatorText("Wynik");
        ImGui::Text("Build: %s (%s)", r.hero_name.c_str(), r.role_name.c_str());
        draw_build_table("build_result", r.build);
    }
}

void draw_counter_tab(AppState& s) {
    ImGui::TextDisabled("Counter-build: dobierz itemy przeciw typowym buildom wrogow.");
    ImGui::Spacing();
    hero_combo("Bohater##counter", s.counter_hero, s.hero_names);
    role_combo("Rola##counter", s.counter_role);
    ImGui::Spacing();
    ImGui::TextUnformatted("Wrogowie (1-5, puste pola pominiete):");
    for (int i = 0; i < 5; ++i) {
        char id[32];
        std::snprintf(id, sizeof(id), "Wrog %d##enemy%d", i + 1, i);
        hero_combo(id, s.enemies[static_cast<size_t>(i)], s.hero_names);
    }
    ImGui::Spacing();

    std::vector<std::string> enemy_names;
    for (const auto& e : s.enemies)
        if (!e.empty())
            enemy_names.push_back(e);

    const bool busy = s.counter_task.running();
    ImGui::BeginDisabled(busy || s.counter_hero.empty() || enemy_names.empty());
    if (ImGui::Button("Generuj counter", ImVec2(180, 0))) {
        std::string hero = s.counter_hero;
        Role role = kRoles[static_cast<size_t>(s.counter_role)].role;
        Advisor* adv = &s.advisor;
        s.counter_task.start(
            [adv, hero, role, enemy_names] { return adv->counter(hero, role, enemy_names); });
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("pobieranie buildow wrogow...");
    }

    if (!s.counter_task.error().empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Blad: %s",
                           s.counter_task.error().c_str());
    } else if (s.counter_task.has_result()) {
        const auto& r = s.counter_task.result();
        ImGui::SeparatorText("Sklad wroga");
        for (const auto& b : r.enemy_builds) {
            std::string items;
            for (size_t i = 0; i < b.items.size(); ++i)
                items += (i ? ", " : "") + b.items[i].display_name;
            ImGui::BulletText("%s - \"%s\": %s", b.hero_name.c_str(), b.title.c_str(),
                              items.c_str());
            ImGui::Indent();
            ImGui::TextDisabled("profil: %s%s%s%s", b.is_magical() ? "magiczny " : "fizyczny ",
                                b.crit_heavy() ? "+kryt " : "",
                                b.sustain_heavy() ? "+sustain " : "", b.tanky() ? "+pancerz" : "");
            ImGui::Unindent();
        }
        for (const auto& name : r.skipped_enemies)
            ImGui::TextDisabled("(%s - brak buildow spolecznosci, pominiety)", name.c_str());

        ImGui::Text("Profil wroga: %.0f%% obrazen fizycznych%s%s%s",
                    100.0 * r.profile.physical_ratio, r.profile.has_healing ? ", leczenie" : "",
                    r.profile.has_crit ? ", kryt" : "", r.profile.has_tanks ? ", tanki" : "");

        ImGui::SeparatorText("Counter-build");
        ImGui::Text("%s (%s)", r.hero_name.c_str(), r.role_name.c_str());
        draw_build_table("counter_result", r.counter);
    }
}

#ifdef PREDEYE_HAS_VISION
// Wczytaj klatke z pliku PNG do zakladki kalibracji (synchronicznie — lokalne).
void calib_load_image(AppState::CalibTab& c, const std::string& path) {
    try {
        c.frame = FileCapture(path).grab();
        c.has_frame = true;
        c.dirty = true;
        // Startowa siatka dla tej rozdzielczosci (chyba ze wczytano juz JSON).
        if (c.calib.enemy_item_grid.cols == 0 || c.calib.resolution != c.frame.size())
            c.calib = Calibration::default_for(c.frame.size());
        c.status = "Wczytano zrzut " + std::to_string(c.frame.cols) + "x" +
                   std::to_string(c.frame.rows) + ".";
    } catch (const std::exception& ex) {
        c.status = std::string("Blad wczytania: ") + ex.what();
    }
}

void draw_calibrate_tab(AppState& s) {
    auto& c = s.calib;
    ImGui::TextDisabled("Dopasuj siatke slotow itemow wroga do zrzutu scoreboardu (TAB). "
                        "Kliknij na obraz, aby ustawic origin (lewy-gorny slot [0][0]).");
    ImGui::Spacing();

    // --- Zrodlo klatki ---
    ImGui::InputText("Zrzut PNG##calib", c.image_path, sizeof(c.image_path));
    ImGui::SameLine();
    if (ImGui::Button("Wczytaj##calibimg") && c.image_path[0] != '\0')
        calib_load_image(c, c.image_path);

#ifdef _WIN32
    ImGui::SameLine();
    const bool shooting = c.shot_task.running();
    ImGui::BeginDisabled(shooting);
    if (ImGui::Button("Zrzut z gry (3 s)##calibshot")) {
        c.status = "Przelacz sie do gry i PRZYTRZYMAJ TAB — zrzut za 3 s...";
        c.shot_consumed = false;
        c.shot_task.start([] {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            return DxgiCapture().grab();
        });
    }
    ImGui::EndDisabled();
    if (shooting) {
        ImGui::SameLine();
        ImGui::TextDisabled("odliczanie...");
    }
#endif

    // --- Plik kalibracji ---
    ImGui::InputText("Plik JSON##calib", c.config_path, sizeof(c.config_path));
    ImGui::SameLine();
    if (ImGui::Button("Wczytaj JSON##calibcfg")) {
        try {
            c.calib = Calibration::load(c.config_path);
            c.dirty = true;
            c.status = std::string("Wczytano ") + c.config_path + ".";
        } catch (const std::exception& ex) {
            c.status = std::string("Blad JSON: ") + ex.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Zapisz JSON##calibcfg")) {
        try {
            c.calib.save(c.config_path);
            c.status = std::string("Zapisano ") + c.config_path + ".";
        } catch (const std::exception& ex) {
            c.status = std::string("Blad zapisu: ") + ex.what();
        }
    }
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

    // --- Suwaki siatki ---
    auto& g = c.calib.enemy_item_grid;
    const int max_x = c.has_frame ? c.frame.cols : 4096;
    const int max_y = c.has_frame ? c.frame.rows : 4096;
    bool changed = false;
    changed |= ImGui::DragInt("origin X", &g.origin.x, 1.0f, 0, max_x);
    changed |= ImGui::DragInt("origin Y", &g.origin.y, 1.0f, 0, max_y);
    changed |= ImGui::DragInt("slot szer.", &g.slot.width, 1.0f, 4, 256);
    changed |= ImGui::DragInt("slot wys.", &g.slot.height, 1.0f, 4, 256);
    changed |= ImGui::DragInt("dx (krok w wierszu)", &g.dx, 1.0f, 0, max_x);
    changed |= ImGui::DragInt("dy (krok wierszy)", &g.dy, 1.0f, 0, max_y);
    changed |= ImGui::DragInt("kolumny", &g.cols, 0.1f, 1, 10);
    changed |= ImGui::DragInt("wiersze", &g.rows, 0.1f, 1, 8);
    if (changed)
        c.dirty = true;

    if (!c.has_frame) {
        ImGui::Spacing();
        ImGui::TextDisabled("Wczytaj zrzut PNG (lub zrob zrzut z gry), aby zobaczyc podglad.");
        return;
    }

    // --- Podglad z siatka ---
    if (c.dirty) {
        c.tex.upload(draw_grid(c.frame, c.calib));
        c.dirty = false;
    }
    const float avail = ImGui::GetContentRegionAvail().x;
    const float scale = (c.tex.w > 0) ? std::min(1.0f, avail / static_cast<float>(c.tex.w)) : 1.0f;
    const ImVec2 disp(c.tex.w * scale, c.tex.h * scale);
    ImGui::Image((ImTextureID)(intptr_t)c.tex.id, disp);

    // Klik na obrazie -> ustaw origin w pikselach pelnej rozdzielczosci.
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

void draw_live_tab(AppState& s) {
    auto& lv = s.live;
    ImGui::TextDisabled("Odczyt itemow wroga ze scoreboardu -> profil -> counter-build. "
                        "Wymaga kalibracji i bazy ikon (pierwszy odczyt ja pobierze).");
    ImGui::Spacing();

    hero_combo("Bohater##live", lv.hero, s.hero_names);
    role_combo("Rola##live", lv.role);
    ImGui::InputText("Plik kalibracji##live", lv.config_path, sizeof(lv.config_path));
#ifndef _WIN32
    ImGui::InputText("Zrzut PNG##live", lv.image_path, sizeof(lv.image_path));
#else
    ImGui::InputText("Zrzut PNG (opcjonalnie)##live", lv.image_path, sizeof(lv.image_path));
    ImGui::TextDisabled("Puste pole PNG = zrzut z gry (DXGI). Przytrzymaj TAB przed odczytem.");
#endif
    ImGui::Spacing();

    const bool busy = lv.read_task.running();
    const bool need_png = std::string(lv.image_path).empty();
#ifndef _WIN32
    const bool can_read = !lv.hero.empty() && !need_png;
#else
    const bool can_read = !lv.hero.empty();
#endif
    ImGui::BeginDisabled(busy || !can_read);
    if (ImGui::Button("Odczytaj scoreboard", ImVec2(200, 0))) {
        VisionSession* vs = &s.vision;
        std::string hero = lv.hero;
        Role role = kRoles[static_cast<size_t>(lv.role)].role;
        std::string cfg = lv.config_path;
        std::string img = lv.image_path;
        // Ustaw cel tylko przy zmianie (set_objective resetuje diff).
        const bool set_obj = (hero != lv.applied_hero || lv.role != lv.applied_role);
        lv.applied_hero = hero;
        lv.applied_role = lv.role;
        lv.read_consumed = false;
        lv.read_task.start([vs, hero, role, cfg, img, set_obj]() -> LiveResult {
            if (set_obj)
                vs->set_objective(hero, role);
            if (!std::filesystem::exists(cfg))
                throw std::runtime_error("brak " + cfg +
                                         " — najpierw skalibruj (zakladka Kalibracja)");
            const Calibration calib = Calibration::load(cfg);
            cv::Mat frame;
            if (!img.empty()) {
                frame = FileCapture(img).grab();
            } else {
#ifdef _WIN32
                frame = DxgiCapture().grab();
#else
                throw std::runtime_error("podaj zrzut PNG — zrzut z gry dziala tylko na Windows");
#endif
            }
            return vs->read(frame, calib);
        });
    }
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("odczyt / pobieranie bazy ikon...");
    }
    if (s.vision.matcher_ready()) {
        ImGui::SameLine();
        ImGui::TextDisabled("baza: %d ikon", static_cast<int>(s.vision.icon_base_size()));
    }

    if (!lv.read_task.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Blad: %s",
                           lv.read_task.error().c_str());
    if (lv.read_task.has_result() && !lv.read_consumed) {
        lv.last = lv.read_task.result();
        lv.read_consumed = true;
    }

    if (!lv.last)
        return;

    const LiveResult& r = *lv.last;
    ImGui::SeparatorText("Odczyt");
    ImGui::Text("%d itemow (%d niepewnych)", r.total_items, r.uncertain);
    for (const auto& e : r.enemies) {
        std::string line;
        for (const auto& slot : e.slots) {
            if (slot.empty)
                continue;
            if (!line.empty())
                line += ", ";
            line += slot.name;
            if (!slot.confident)
                line += " (niepewne)";
        }
        ImGui::BulletText("Wrog %d: %s", e.row + 1, line.empty() ? "(pusto)" : line.c_str());
        if (!e.changes.empty()) {
            std::string delta;
            for (const auto& ch : e.changes)
                delta += (delta.empty() ? "" : ", ") + ch;
            ImGui::Indent();
            ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1.0f), "zmiana: %s", delta.c_str());
            ImGui::Unindent();
        }
    }

    ImGui::Text("Profil wroga: %.0f%% obrazen fizycznych%s%s%s", 100.0 * r.profile.physical_ratio,
                r.profile.has_healing ? ", leczenie" : "", r.profile.has_crit ? ", kryt" : "",
                r.profile.has_tanks ? ", tanki" : "");

    ImGui::SeparatorText("Counter-build");
    draw_build_table("live_counter", r.counter);
}
#endif // PREDEYE_HAS_VISION

void draw_main_window(AppState& s) {
    // Jedno okno wypelniajace obszar aplikacji.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    ImGui::Begin("predeye", nullptr, flags);

    ImGui::TextUnformatted("predeye - asystent-trener Predecessor");
    ImGui::SameLine();
    if (s.hero_names_task.running())
        ImGui::TextDisabled("(laduje liste bohaterow z Omeda.city...)");
    else if (s.hero_names.empty() && !s.hero_names_task.error().empty())
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "(brak listy bohaterow: %s - mozna wpisac nazwe recznie)",
                           s.hero_names_task.error().c_str());
    else if (!s.hero_names.empty())
        ImGui::TextDisabled("(%d bohaterow zaladowanych)", static_cast<int>(s.hero_names.size()));
    ImGui::Separator();

    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem("Build")) {
            draw_build_tab(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Counter")) {
            draw_counter_tab(s);
            ImGui::EndTabItem();
        }
#ifdef PREDEYE_HAS_VISION
        if (ImGui::BeginTabItem("Kalibracja")) {
            draw_calibrate_tab(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Live")) {
            draw_live_tab(s);
            ImGui::EndTabItem();
        }
#endif
        ImGui::EndTabBar();
    }
    ImGui::End();
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

    GLFWwindow* window = glfwCreateWindow(1000, 720, "predeye", nullptr, nullptr);
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
    // Start: pobierz liste bohaterow w tle (rozgrzewa tez cache itemow).
    {
        Advisor* adv = &state.advisor;
        state.hero_names_task.start([adv] { return adv->hero_names(); });
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Odbior wynikow z watkow tla.
        state.hero_names_task.poll();
        if (state.hero_names.empty() && state.hero_names_task.has_result())
            state.hero_names = state.hero_names_task.result();
        state.build_task.poll();
        state.counter_task.poll();
#ifdef PREDEYE_HAS_VISION
        state.live.read_task.poll();
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
#endif

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
