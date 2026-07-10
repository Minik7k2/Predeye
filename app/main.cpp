// predeye — asystent-trener do gry Predecessor (doradca obok gry, zero ingerencji).
#include "core/build_engine.hpp"
#include "core/enemy_build.hpp"
#include "core/hero_context.hpp"
#include "core/models.hpp"
#include "core/omeda_client.hpp"
#ifdef PREDEYE_HAS_VISION
#include "vision/calibration.hpp"
#include "vision/capture.hpp"
#include "vision/icon_matcher.hpp"
#ifdef _WIN32
#include "vision/capture_dxgi.hpp"
#endif
#include <chrono>
#include <filesystem>
#include <opencv2/imgcodecs.hpp>
#include <thread>
#endif

#include <cstdio>
#include <exception>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

using namespace predeye;

constexpr const char* kVersion = "0.1.0";

void print_help() {
    std::printf("predeye %s — asystent-trener do gry Predecessor\n\n", kVersion);
    std::printf("Uzycie:\n");
    std::printf("  predeye build   \"<bohater>\" <rola>                       "
                "build pod cel (bohater + rola)\n");
    std::printf("  predeye counter \"<bohater>\" <rola> <wrog1> ... <wrog5>   "
                "counter-build pod sklad wroga\n");
    std::printf("  predeye fetch-icons                                      "
                "pobranie bazy ikon itemow (M2)\n");
    std::printf("  predeye calibrate [--image <png>] [--config <json>]      "
                "kalibracja siatki scoreboardu\n");
    std::printf("  predeye live    \"<bohater>\" <rola>                       "
                "tryb live z odczytem ekranu (M5)\n");
    std::printf("\nRole: offlane | jungle | midlane | carry | support\n");
}

void print_build(const BuildResult& res) {
    for (const auto& p : res.items)
        std::printf("  %-24s %5d zl  ocena %7.1f  [%s]  %s\n", p.item.display_name.c_str(),
                    p.item.total_price, p.score, p.item.slot_type.c_str(), p.reason.c_str());
    std::printf("  ------\n  %s\n", res.summary.c_str());
}

Role require_role(const std::string& arg) {
    Role role = parse_role(arg);
    if (role == Role::Unknown)
        throw std::runtime_error("nieznana rola: \"" + arg +
                                 "\" (offlane|jungle|midlane|carry|support)");
    return role;
}

int cmd_build(const std::string& hero_name, const std::string& role_arg) {
    OmedaClient api;
    HeroDB db(api.heroes());
    const auto me = db.by_names({hero_name}).front();
    const Role role = require_role(role_arg);

    BuildEngine engine(parse_items(api.items()));
    const Objective obj = objective_for(me, role);
    std::printf("Build: %s (%s)\n", me.name.c_str(), role_name(role).c_str());
    print_build(engine.optimize(obj));
    return 0;
}

int cmd_counter(const std::string& hero_name, const std::string& role_arg,
                const std::vector<std::string>& enemy_names) {
    OmedaClient api;
    HeroDB db(api.heroes());
    const auto me = db.by_names({hero_name}).front();
    const Role role = require_role(role_arg);
    const auto enemies = db.by_names(enemy_names);

    const auto items = parse_items(api.items());
    const auto index = index_by_id(items);

    // Zgrubny profil z klas, doostrzony typowymi buildami z API.
    EnemyProfile profile = enemy_from(enemies);
    std::vector<EnemyBuildProfile> enemy_builds;
    for (const auto& e : enemies) {
        auto tb = typical_build(api, index, e.id, e.name);
        if (!tb) {
            std::fprintf(stderr, "predeye: brak buildow spolecznosci dla %s — pomijam\n",
                         e.name.c_str());
            continue;
        }
        enemy_builds.push_back(std::move(*tb));
    }
    refine_enemy_profile(profile, enemy_builds);

    std::printf("Sklad wroga:\n");
    for (const auto& b : enemy_builds) {
        std::printf("  %-12s typowy build \"%s\": ", b.hero_name.c_str(), b.title.c_str());
        for (size_t i = 0; i < b.items.size(); ++i)
            std::printf("%s%s", i ? ", " : "", b.items[i].display_name.c_str());
        std::printf("\n    profil: %s%s%s%s\n", b.is_magical() ? "magiczny " : "fizyczny ",
                    b.crit_heavy() ? "+kryt " : "", b.sustain_heavy() ? "+sustain " : "",
                    b.tanky() ? "+pancerz" : "");
    }
    std::printf("Profil wroga: %.0f%% obrazen fizycznych%s%s%s\n\n",
                100.0 * profile.physical_ratio, profile.has_healing ? ", leczenie" : "",
                profile.has_crit ? ", kryt" : "", profile.has_tanks ? ", tanki" : "");

    BuildEngine engine(items);
    const Objective obj = objective_for(me, role);
    std::printf("Counter-build: %s (%s)\n", me.name.c_str(), role_name(role).c_str());
    print_build(engine.counter_build(obj, profile));
    return 0;
}

#ifdef PREDEYE_HAS_VISION
// Kalibracja (§6.8): zrzut (odliczanie, uzytkownik trzyma TAB) -> PNG;
// nastepnie podglad: klatka + calibration.json -> preview.png z narysowana
// siatka. Uzytkownik iteruje wartosci w JSON, az siatka trafi w sloty.
int cmd_calibrate(const std::vector<std::string>& args) {
    std::string image_path, config_path = "calibration.json";
    std::string shot_path = "calibration_shot.png", preview_path = "preview.png";
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--image" && i + 1 < args.size())
            image_path = args[++i];
        else if (args[i] == "--config" && i + 1 < args.size())
            config_path = args[++i];
        else
            throw std::runtime_error("nieznany argument: " + args[i] +
                                     " (uzycie: calibrate [--image <png>] [--config <json>])");
    }

    // 1) Klatka: z pliku (--image lub wczesniejszy zrzut) albo swiezy zrzut.
    cv::Mat frame;
    if (image_path.empty() && std::filesystem::exists(shot_path)) {
        image_path = shot_path;
        std::printf("Uzywam istniejacego zrzutu %s (usun go, by zrobic nowy).\n",
                    shot_path.c_str());
    }
    if (!image_path.empty()) {
        frame = FileCapture(image_path).grab();
    } else {
#ifdef _WIN32
        std::printf("Nacisnij Enter, przelacz sie do gry i PRZYTRZYMAJ TAB — "
                    "zrzut za 5 sekund...\n");
        std::getchar();
        for (int s = 5; s >= 1; --s) {
            std::printf("  %d...\n", s);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        DxgiCapture cap;
        frame = cap.grab();
        cv::imwrite(shot_path, frame);
        std::printf("Zapisano zrzut: %s (%dx%d)\n", shot_path.c_str(), frame.cols, frame.rows);
#else
        throw std::runtime_error("zrzut ekranu dziala tylko na Windows — podaj --image <png>");
#endif
    }

    // 2) Kalibracja: wczytaj albo zaloz plik startowy dla tej rozdzielczosci.
    Calibration calib;
    if (std::filesystem::exists(config_path)) {
        calib = Calibration::load(config_path);
        if (calib.resolution != frame.size())
            std::fprintf(stderr,
                         "predeye: uwaga: %s jest dla %dx%d, a klatka ma %dx%d — "
                         "dostosuj wartosci lub usun plik\n",
                         config_path.c_str(), calib.resolution.width, calib.resolution.height,
                         frame.cols, frame.rows);
    } else {
        calib = Calibration::default_for(frame.size());
        calib.save(config_path);
        std::printf("Utworzono startowy %s — wartosci orientacyjne, do iteracji.\n",
                    config_path.c_str());
    }

    // 3) Podglad.
    cv::imwrite(preview_path, draw_grid(frame, calib));
    std::printf("Zapisano %s. Obejrzyj go, popraw wartosci w %s i uruchom "
                "calibrate ponownie, az siatka trafi w sloty itemow wrogow.\n",
                preview_path.c_str(), config_path.c_str());
    return 0;
}

// Domyslny katalog bazy ikon: obok cache API, przezywa miedzy sesjami.
std::string default_icon_dir() { return OmedaClient::default_dir() + "/icons"; }

int cmd_fetch_icons() {
    OmedaClient api;
    const auto items = parse_items(api.items());
    const std::string dir = default_icon_dir();
    IconMatcher matcher(items, api, dir); // konstruktor pobiera brakujace ikony
    std::printf("Baza ikon gotowa: %d sygnatur w %s\n", static_cast<int>(matcher.base_size()),
                dir.c_str());
    return 0;
}
#endif // PREDEYE_HAS_VISION

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }
    const std::string cmd = argv[1];
    try {
        if (cmd == "--help" || cmd == "-h" || cmd == "help") {
            print_help();
            return 0;
        }
        if (cmd == "--version") {
            std::printf("predeye %s\n", kVersion);
            return 0;
        }
        if (cmd == "build") {
            if (argc != 4) {
                std::fprintf(stderr, "uzycie: predeye build \"<bohater>\" <rola>\n");
                return 1;
            }
            return cmd_build(argv[2], argv[3]);
        }
        if (cmd == "counter") {
            if (argc < 5) {
                std::fprintf(stderr,
                             "uzycie: predeye counter \"<bohater>\" <rola> <wrog1> [...<wrog5>]\n");
                return 1;
            }
            std::vector<std::string> enemies(argv + 4, argv + argc);
            return cmd_counter(argv[2], argv[3], enemies);
        }
        if (cmd == "fetch-icons" || cmd == "calibrate") {
#ifdef PREDEYE_HAS_VISION
            if (cmd == "fetch-icons")
                return cmd_fetch_icons();
            return cmd_calibrate({argv + 2, argv + argc});
#else
            std::fprintf(stderr,
                         "predeye: komenda \"%s\" wymaga toru wizyjnego — zbuduj z "
                         "-DPREDEYE_VISION=ON\n",
                         cmd.c_str());
            return 1;
#endif
        }
        if (cmd == "live") {
            std::fprintf(stderr, "predeye: komenda \"%s\" bedzie dostepna w kolejnym milestone\n",
                         cmd.c_str());
            return 1;
        }
        std::fprintf(stderr, "predeye: nieznana komenda \"%s\" (zobacz: predeye --help)\n",
                     cmd.c_str());
        return 1;
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "predeye: %s\n", ex.what());
        return 1;
    }
}
