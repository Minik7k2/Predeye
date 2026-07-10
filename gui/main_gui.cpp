// gui/main_gui — graficzna powloka predeye (Dear ImGui + GLFW + OpenGL3).
//
// Prosty doradca "obok gry": zakladka Build (bohater + rola) i Counter
// (bohater + rola + do 5 wrogow). Wszystkie wywolania API ida w tle
// (AsyncTask), by petla renderowania nie zamarzala podczas pobierania danych.
//
// Zero ingerencji w gre — tu tylko wejscie tekstowe i prezentacja wynikow
// rdzenia (§3). Tor wizyjny (capture/scoreboard) pozostaje w CLI.

#include "core/advisor.hpp"
#include "core/hero_context.hpp"
#include "gui/async_task.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cctype>
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
    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_SizingStretchProp;
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
                                b.crit_heavy() ? "+kryt " : "", b.sustain_heavy() ? "+sustain " : "",
                                b.tanky() ? "+pancerz" : "");
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

void draw_main_window(AppState& s) {
    // Jedno okno wypelniajace obszar aplikacji.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus;
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
