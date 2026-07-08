// predeye — asystent-trener do gry Predecessor (doradca obok gry, zero ingerencji).
#include <cstdio>
#include <string>

namespace {

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
    std::printf("  predeye calibrate                                        "
                "kalibracja siatki scoreboardu (M3)\n");
    std::printf("  predeye live    \"<bohater>\" <rola>                       "
                "tryb live z odczytem ekranu (M5)\n");
    std::printf("\nRole: offlane | jungle | midlane | carry | support\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 1;
    }
    const std::string cmd = argv[1];
    if (cmd == "--help" || cmd == "-h" || cmd == "help") {
        print_help();
        return 0;
    }
    if (cmd == "--version") {
        std::printf("predeye %s\n", kVersion);
        return 0;
    }
    std::fprintf(stderr, "predeye: nieznana komenda \"%s\" (zobacz: predeye --help)\n",
                 cmd.c_str());
    return 1;
}
