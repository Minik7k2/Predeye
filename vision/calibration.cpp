#include "calibration.hpp"

#include <fstream>
#include <nlohmann/json.hpp>
#include <opencv2/imgproc.hpp>
#include <stdexcept>

namespace predeye {
namespace {

int req_int(const nlohmann::json& j, const char* key, const std::string& ctx) {
    if (!j.contains(key) || !j[key].is_number())
        throw std::runtime_error(ctx + ": brak pola \"" + key + "\"");
    return j[key].get<int>();
}

GridSpec parse_grid(const nlohmann::json& g, const std::string& ctx) {
    if (!g.contains("origin") || !g["origin"].is_array() || g["origin"].size() != 2 ||
        !g.contains("slot") || !g["slot"].is_array() || g["slot"].size() != 2)
        throw std::runtime_error(ctx + " wymaga pol origin[2] i slot[2]");
    GridSpec spec;
    spec.origin = {g["origin"][0].get<int>(), g["origin"][1].get<int>()};
    spec.slot = {g["slot"][0].get<int>(), g["slot"][1].get<int>()};
    spec.dx = req_int(g, "dx", ctx);
    spec.dy = req_int(g, "dy", ctx);
    spec.cols = req_int(g, "cols", ctx);
    spec.rows = req_int(g, "rows", ctx);
    if (spec.cols <= 0 || spec.rows <= 0 || spec.slot.width <= 0 || spec.slot.height <= 0)
        throw std::runtime_error(ctx + ": wymiary siatki musza byc dodatnie");
    return spec;
}

nlohmann::json dump_grid(const GridSpec& g) {
    return {
        {"origin", {g.origin.x, g.origin.y}},
        {"slot", {g.slot.width, g.slot.height}},
        {"dx", g.dx},
        {"dy", g.dy},
        {"cols", g.cols},
        {"rows", g.rows},
    };
}

// Ramki slotow + numery wierszy jednej siatki, z etykieta nad originem.
void draw_one_grid(cv::Mat& out, const GridSpec& g, const cv::Scalar& color,
                   const std::string& label) {
    const cv::Scalar yellow(0, 255, 255);
    for (int r = 0; r < g.rows; ++r) {
        for (int c = 0; c < g.cols; ++c)
            cv::rectangle(out, g.slot_rect(r, c), color, 1);
        const cv::Rect first = g.slot_rect(r, 0);
        cv::putText(out, std::to_string(r + 1), {first.x - 22, first.y + first.height / 2 + 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.55, yellow, 1);
    }
    // Krzyzyk na origin — najlatwiejszy punkt odniesienia przy iteracji.
    cv::drawMarker(out, g.origin, yellow, cv::MARKER_CROSS, 14, 1);
    cv::putText(out, label, {g.origin.x, g.origin.y - 10}, cv::FONT_HERSHEY_SIMPLEX, 0.5, color,
                1);
}

} // namespace

GridSpec mirror_grid(const GridSpec& enemy, const cv::Size& resolution) {
    GridSpec ally = enemy;
    // Prawy kraniec siatki wroga odbity w lewy kraniec siatki sojusznikow.
    const int span = (enemy.cols - 1) * enemy.dx + enemy.slot.width;
    ally.origin.x = resolution.width - enemy.origin.x - span;
    return ally;
}

Calibration Calibration::load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("nie mozna otworzyc " + path);
    auto j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        throw std::runtime_error(path + ": niepoprawny JSON");

    Calibration c;
    if (j.contains("resolution") && j["resolution"].is_array() && j["resolution"].size() == 2)
        c.resolution = {j["resolution"][0].get<int>(), j["resolution"][1].get<int>()};
    if (!j.contains("enemy_item_grid") || !j["enemy_item_grid"].is_object())
        throw std::runtime_error(path + ": brak obiektu \"enemy_item_grid\"");
    c.enemy_item_grid = parse_grid(j["enemy_item_grid"], path + ": enemy_item_grid");

    // Kompatybilnosc ze starszymi plikami (tylko siatka wroga): sojusznicy
    // dostaja lustrzana siatke orientacyjna — uzytkownik dostroi podgladem.
    if (j.contains("ally_item_grid") && j["ally_item_grid"].is_object())
        c.ally_item_grid = parse_grid(j["ally_item_grid"], path + ": ally_item_grid");
    else
        c.ally_item_grid = mirror_grid(c.enemy_item_grid, c.resolution);
    return c;
}

void Calibration::save(const std::string& path) const {
    nlohmann::json j;
    j["resolution"] = {resolution.width, resolution.height};
    j["enemy_item_grid"] = dump_grid(enemy_item_grid);
    j["ally_item_grid"] = dump_grid(ally_item_grid);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        throw std::runtime_error("nie mozna zapisac " + path);
    out << j.dump(2) << "\n";
}

Calibration Calibration::default_for(const cv::Size& resolution) {
    // Wartosci startowe skalowane z bazy 1080p, oszacowane wzrokowo z realnych
    // zrzutow scoreboardu uzytkownika (borderless 1920x1080, patch 5.4.4):
    // panel wroga po prawej, 5 wierszy, siatka itemow za slotem crestu.
    // Nadal przyblizone — uzytkownik dostraja podgladem (preview.png).
    const double s = resolution.height / 1080.0;
    Calibration c;
    c.resolution = resolution;
    c.enemy_item_grid.origin = {static_cast<int>(1346 * s), static_cast<int>(256 * s)};
    c.enemy_item_grid.slot = {static_cast<int>(46 * s), static_cast<int>(46 * s)};
    c.enemy_item_grid.dx = static_cast<int>(59 * s);
    c.enemy_item_grid.dy = static_cast<int>(145 * s);
    // Na zrzutach widac 7 kwadratow na itemy (spec zakladal 6) — do
    // potwierdzenia przy kalibracji; nadmiarowe puste sloty odfiltruje
    // looks_empty.
    c.enemy_item_grid.cols = 7;
    c.enemy_item_grid.rows = 5;
    c.ally_item_grid = mirror_grid(c.enemy_item_grid, resolution);
    return c;
}

cv::Mat draw_grid(const cv::Mat& frame_bgr, const Calibration& calib) {
    cv::Mat out = frame_bgr.clone();
    draw_one_grid(out, calib.enemy_item_grid, cv::Scalar(0, 0, 255), "WROGOWIE");
    draw_one_grid(out, calib.ally_item_grid, cv::Scalar(0, 255, 0), "MOJA DRUZYNA");
    return out;
}

} // namespace predeye
