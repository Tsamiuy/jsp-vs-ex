// Host test of installer/port/jsp/jsp_version_line.h: text and value choice. No game, no GPU.
#include "../port/jsp/jsp_version_line.h"
#include <cstdio>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); fails++; } } while (0)
using namespace jpv;
int main()
{
    CHECK(parse_version_txt("0.1.0\n") == "0.1.0" && parse_version_txt("0.1.0\r\nrest\n") == "0.1.0");
    CHECK(parse_version_txt("\xEF\xBB\xBF" "0.1.0-rc1\n") == "0.1.0-rc1");
    CHECK(parse_version_txt("").empty() && parse_version_txt("\n0.1\n").empty() && parse_version_txt("a b\n").empty());
    CHECK(parse_version_txt("<html>\n").empty() && parse_version_txt(std::string(30, '1')).empty());

    Inputs in;
    CHECK(!choose(in).any());                                   // dev build: nothing
    in.switch_env = "dev";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX dev");
    in.switch_env = "0"; in.port_version = "0.1.0";
    CHECK(!choose(in).any());                                   // off beats everything
    in.switch_env = nullptr;
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.1.0");
    in.port_version = nullptr; in.file_version = "0.1.0";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.1.0");   // from release.txt
    in.port_version = "0.3.0";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.3.0");   // env beats the file
    in.port_version = "no good";
    CHECK(choose(in).line1 == "J-Stars Victory VS-EX 0.1.0");   // invalid env ignored
    in.port_version = nullptr;

    CHECK(top_menu(true, true, 0, 0) && top_menu(true, true, 2, 0));
    CHECK(!top_menu(false, true, 0, 0) && !top_menu(true, false, 0, 0) && !top_menu(true, true, 3, 0));   // scene gone / stale
    for (int s = 1; s <= 5; s++) CHECK(!top_menu(true, true, 0, s));                                   // a sub-menu panel is up
    if (!fails) std::printf("version line: all checks pass\n");
    return fails ? 1 : 0;
}
