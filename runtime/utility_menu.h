#pragma once
#include <string>
#include <vector>

namespace edutility {
struct Row {std::string text;bool selected=false;};
struct Menu {
    std::string title;
    std::vector<Row> rows,details;
    std::vector<std::string> footer;
    std::string notice;
    unsigned focus=0;
};
} // namespace edutility
