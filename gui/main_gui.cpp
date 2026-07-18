// gui/main_gui — graficzna powloka predeye (Dear ImGui + GLFW + OpenGL3).
//
// Zakladki: Kalibracja (siatki slotow OBU druzyn na podgladzie zrzutu; zrzut
// z pliku — dialog/drag&drop/galeria katalogu — albo z gry) oraz Live (odczyt
// itemow obu druzyn ze scoreboardu -> profil wroga -> counter-build z diffem).
// Budowane wylacznie z torem wizyjnym (patrz CMakeLists w korzeniu). Wszystkie
// wywolania API/wizji ida w tle (AsyncTask), by petla renderowania nie
// zamarzala. Build/counter z recznym wpisywaniem skladow zostaly w CLI.
//
// Zero ingerencji w gre (§3): jedyne wejscie wizualne to zrzut ekranu przez
// publiczne API systemu, prezentacja to wynik rdzenia.

#include "core/advisor.hpp"
#include "core/hero_context.hpp"
#include "gui/async_task.hpp"
#include "vision/auto_calibration.hpp"
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

// Tabela jednej druzyny z odczytu live: rola | nick | itemy | zmiana od
// poprzednika. Ta sama rola w tabeli wrogow i sojusznikow = przeciwnicy
// w linii (matchup). `nicks` = bufory nicków per wiersz (edycja w tabeli).
void draw_team_table(const char* id, const char* title, const std::vector<LiveRowView>& rows,
                     char (*nicks)[48]) {
    ImGui::SeparatorText(title);
    const ImGuiTableFlags flags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable(id, 4, flags))
        return;
    ImGui::TableSetupColumn("Rola", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Nick", ImGuiTableColumnFlags_WidthStretch, 0.9f);
    ImGui::TableSetupColumn("Itemy", ImGuiTableColumnFlags_WidthStretch, 3.2f);
    ImGui::TableSetupColumn("Zmiana", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableHeadersRow();
    for (const auto& r : rows) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        // Bohater z portretu; "?" gdy dopasowanie ponizej progu pewnosci.
        std::string who = r.role_label;
        if (!r.hero_name.empty())
            who += " " + r.hero_name + (r.hero_confident ? "" : "?");
        ImGui::TextUnformatted(who.c_str());
        ImGui::TableSetColumnIndex(1);
        if (r.row >= 0 && r.row < 8) {
            ImGui::PushID(r.row);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##nick", "nick...", nicks[r.row], sizeof(nicks[r.row]));
            ImGui::PopID();
        }
        ImGui::TableSetColumnIndex(2);
        std::string line;
        for (const auto& s : r.slots) {
            if (s.empty)
                continue;
            if (!line.empty())
                line += ", ";
            line += s.name;
            if (!s.confident)
                line += " (niepewne)";
        }
        ImGui::TextWrapped("%s", line.empty() ? "(pusto)" : line.c_str());
        ImGui::TableSetColumnIndex(3);
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

// --- Stan aplikacji ----------------------------------------------------------
struct AppState {
    // Advisor tylko do listy bohaterow — osobna instancja od VisionSession,
    // by watek listy nie scigal sie z watkiem odczytu live o wspolny stan.
    Advisor advisor;
    gui::AsyncTask<std::vector<std::string>> hero_names_task;
    std::vector<std::string> hero_names;

    // Wspolna sesja toru wizyjnego (dane API + baza ikon + stan diffa).
    VisionSession vision;

    GLFWwindow* window = nullptr; // wlasciciel natywnego dialogu pliku

    // Zakladka Kalibracja.
    struct CalibTab {
        Calibration calib;
        cv::Mat frame; // wczytany zrzut (BGR)
        bool has_frame = false;
        bool dirty = true; // przelicz overlay + upload tekstury
        GlTexture tex;
        char image_path[512] = "";
        char config_path[512] = "calibration.json";
        std::string status;
        int edit_grid = 0;                // 0 = wrogowie, 1 = moja druzyna
        std::vector<std::string> gallery; // obrazy w katalogu ostatniego zrzutu (UTF-8)
        int gallery_idx = -1;             // pozycja biezacego zrzutu w galerii
        gui::AsyncTask<cv::Mat> shot_task; // zrzut z gry (DXGI, z odliczaniem)
        bool shot_consumed = true;         // czy wynik zrzutu juz odebrano
    } calib;

    // Zakladka Live.
    struct LiveTab {
        std::string hero;
        int role = 0;
        char config_path[512] = "calibration.json";
        std::string applied_hero; // ostatnio ustawiony cel (do wykrycia zmiany)
        int applied_role = -1;
        gui::AsyncTask<LiveResult> read_task;
        bool read_consumed = true; // czy wynik odczytu juz odebrano
        bool f9_down = false;      // do wykrycia zbocza globalnego hotkeya F9
        std::optional<LiveResult> last;
        std::string status;
        // Nicki graczy per wiersz scoreboardu (wpisywane recznie w tabeli —
        // nick to dowolny tekst, ktorego nie rozpoznamy ze zrzutu bez OCR/M7).
        // Wiersze scoreboardu sa stale w obrebie meczu, wiec nick trzyma sie
        // gracza miedzy odczytami; zmiana celu (nowy mecz) je czysci.
        char enemy_nicks[8][48] = {};
        char ally_nicks[8][48] = {};
    } live;
};

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
    ofn.lpstrTitle = L"Wybierz zrzut scoreboardu";
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

void draw_calibrate_tab(AppState& s) {
    auto& c = s.calib;
    ImGui::TextDisabled("Dopasuj siatki slotow do zrzutu scoreboardu (TAB): czerwona = wrogowie, "
                        "zielona = moja druzyna. Klik na obraz ustawia origin edytowanej siatki.");
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
        ImGui::SameLine();
        if (ImGui::Button("Wykryj automatycznie##calib")) {
            // detect_grids to szybki skan RLE jasnosci (pojedyncze ms na 1080p)
            // — nie wymaga watku tla ani bazy ikon.
            if (const auto det = detect_grids(c.frame)) {
                c.calib = det->calib;
                c.dirty = true;
                c.status = "Auto-detekcja: sojusznicy " + std::to_string(det->rows_ally) +
                           " wierszy (" + std::to_string(det->lines_ally) +
                           " linii wzoru), wrogowie " + std::to_string(det->rows_enemy) +
                           " wierszy (" + std::to_string(det->lines_enemy) + " linii)" +
                           (det->note.empty() ? "" : "; " + det->note) +
                           ". Sprawdz podglad i zapisz JSON.";
            } else {
                c.status = "Auto-detekcja nie znalazla spojnej siatki na tej klatce — "
                           "ustaw recznie (suwaki/klik) albo wczytaj ostrzejszy zrzut "
                           "z widocznym scoreboardem.";
            }
        }
    }

    if (!c.status.empty())
        ImGui::TextWrapped("%s", c.status.c_str());
    ImGui::Separator();

    // --- Wybor edytowanej siatki + suwaki ---
    ImGui::TextUnformatted("Edytowana siatka:");
    ImGui::SameLine();
    ImGui::RadioButton("Wrogowie##grid", &c.edit_grid, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Moja druzyna##grid", &c.edit_grid, 1);
    ImGui::SameLine();
    if (ImGui::Button("Kopiuj geometrie do drugiej siatki##grid")) {
        // Przenosi slot/dx/dy/cols/rows, zostawia origin drugiej siatki —
        // panele druzyn maja identyczna geometrie, rozny punkt startu.
        const GridSpec& from =
            (c.edit_grid == 0) ? c.calib.enemy_item_grid : c.calib.ally_item_grid;
        GridSpec& to = (c.edit_grid == 0) ? c.calib.ally_item_grid : c.calib.enemy_item_grid;
        const cv::Point keep = to.origin;
        to = from;
        to.origin = keep;
        c.dirty = true;
    }

    auto& g = (c.edit_grid == 0) ? c.calib.enemy_item_grid : c.calib.ally_item_grid;
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

// Startuje odczyt scoreboardu w tle — wspolna sciezka przycisku i globalnego
// hotkeya F9. Nic nie robi, gdy odczyt juz trwa albo nie wybrano bohatera.
// Klatka zawsze z gry (DXGI) — odczyt testowy z PNG zostal w CLI
// (`predeye live --image`); w GUI zrzutom z pliku sluzy zakladka Kalibracja.
void start_live_read(AppState& s) {
    auto& lv = s.live;
    if (lv.read_task.running() || lv.hero.empty())
        return;
    VisionSession* vs = &s.vision;
    std::string hero = lv.hero;
    Role role = kRoles[static_cast<size_t>(lv.role)].role;
    std::string cfg = lv.config_path;
    // Ustaw cel tylko przy zmianie (set_objective resetuje diff); nowy cel
    // = nowy mecz, wiec nicki poprzedniego skladu przestaja obowiazywac.
    const bool set_obj = (hero != lv.applied_hero || lv.role != lv.applied_role);
    if (set_obj) {
        std::memset(lv.enemy_nicks, 0, sizeof(lv.enemy_nicks));
        std::memset(lv.ally_nicks, 0, sizeof(lv.ally_nicks));
    }
    lv.applied_hero = hero;
    lv.applied_role = lv.role;
    lv.read_consumed = false;
    lv.read_task.start([vs, hero, role, cfg, set_obj]() -> LiveResult {
        if (set_obj)
            vs->set_objective(hero, role);
        if (!std::filesystem::exists(cfg))
            throw std::runtime_error("brak " + cfg +
                                     " — najpierw skalibruj (zakladka Kalibracja)");
#ifdef _WIN32
        return vs->read(DxgiCapture().grab(), Calibration::load(cfg));
#else
        throw std::runtime_error("tryb live wymaga zrzutu z gry (DXGI), "
                                 "dostepnego tylko na Windows");
#endif
    });
}

void draw_live_tab(AppState& s) {
    auto& lv = s.live;
    ImGui::TextDisabled("Odczyt itemow obu druzyn ze scoreboardu -> profil wroga -> counter-build. "
                        "Wymaga kalibracji i bazy ikon (pierwszy odczyt ja pobierze).");
    ImGui::Spacing();

    hero_combo("Bohater##live", lv.hero, s.hero_names);
    role_combo("Rola##live", lv.role);
    ImGui::InputText("Plik kalibracji##live", lv.config_path, sizeof(lv.config_path));
#ifdef _WIN32
    ImGui::TextDisabled("W grze: trzymaj TAB i nacisnij F9 — odczyt uruchomi sie sam\n"
                        "(F9 dziala globalnie, bez klikania w GUI).");
#else
    ImGui::TextDisabled("Tryb live wymaga Windows (zrzut z gry przez DXGI).");
#endif
    ImGui::Spacing();

    const bool busy = lv.read_task.running();
#ifdef _WIN32
    const bool can_read = !lv.hero.empty();
#else
    const bool can_read = false; // brak zrodla klatek poza Windows
#endif
    ImGui::BeginDisabled(busy || !can_read);
    if (ImGui::Button("Odczytaj scoreboard", ImVec2(200, 0)))
        start_live_read(s);
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
    ImGui::TextDisabled("Ta sama rola w obu tabelach = przeciwnicy w linii "
                        "(scoreboard sortuje graczy wg rol).");
    draw_team_table("live_enemies", "Wrogowie", r.enemies, lv.enemy_nicks);
    draw_team_table("live_allies", "Moja druzyna", r.allies, lv.ally_nicks);

    ImGui::Text("Profil wroga: %.0f%% obrazen fizycznych%s%s%s", 100.0 * r.profile.physical_ratio,
                r.profile.has_healing ? ", leczenie" : "", r.profile.has_crit ? ", kryt" : "",
                r.profile.has_tanks ? ", tanki" : "");

    ImGui::SeparatorText("Counter-build");
    draw_build_table("live_counter", r.counter);
}

void draw_main_window(AppState& s) {
#ifdef _WIN32
    // Globalny hotkey F9 (spojny z CLI live): odczyt scoreboardu bez klikania.
    // Scoreboard widac tylko, gdy gracz trzyma TAB z fokusem na grze — GUI nie
    // dostaje wtedy zadnego wejscia, wiec klawisz czytamy GetAsyncKeyState,
    // niezaleznie od fokusu i aktywnej zakladki.
    const bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && !s.live.f9_down)
        start_live_read(s);
    s.live.f9_down = f9;
#endif

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
        if (ImGui::BeginTabItem("Kalibracja")) {
            draw_calibrate_tab(s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Live")) {
            draw_live_tab(s);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

// Przeciagniecie pliku na okno -> wczytaj do zakladki Kalibracja.
// GLFW dostarcza sciezki w UTF-8 (spojnie z FileCapture).
void drop_callback(GLFWwindow* window, int count, const char** paths) {
    if (count <= 0)
        return;
    auto* s = static_cast<AppState*>(glfwGetWindowUserPointer(window));
    if (!s)
        return;
    calib_load_image(s->calib, paths[0]);
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
    state.window = window;
    glfwSetWindowUserPointer(window, &state);
    glfwSetDropCallback(window, drop_callback); // backend ImGui nie uzywa drop
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
