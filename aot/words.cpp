#include <cstdio>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

// Same rule as words.tpth_s: ASCII letters, A-Z folded to a-z, first-seen order.
int main() {
    const char *path = "/home/ash/Downloads/lipsum-cicero.txt";
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::unordered_map<std::string, int> counts;
    std::vector<std::string> order;
    std::string word;
    auto flush = [&]() {
        if (word.empty()) return;
        auto it = counts.find(word);
        if (it == counts.end()) {
            counts.emplace(word, 1);
            order.push_back(word);
        } else {
            it->second += 1;
        }
        word.clear();
    };
    for (unsigned char c : text) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
            word.push_back((char)c);
        } else {
            flush();
        }
    }
    flush();
    std::fputs("words, instances\n", stdout);
    for (const std::string &w : order) {
        std::printf("%s: %d\n", w.c_str(), counts[w]);
    }
    return 0;
}
