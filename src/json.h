// Tiny JSON reader/writer - just enough for the OPlus ".metadata" file, which is
// kept compatible with the original PHP tool.
#pragma once

#include <string>
#include <utility>
#include <vector>

struct JVal {
    enum Type { Null, Bool, Num, Str, Arr, Obj } type = Null;
    bool b = false;
    double n = 0;
    std::string s;  // string value, or the raw token text for numbers
    std::vector<JVal> a;
    std::vector<std::pair<std::string, JVal>> o;

    const JVal* get(const std::string& key) const {
        for (auto& kv : o)
            if (kv.first == key) return &kv.second;
        return nullptr;
    }
};

bool parseJson(const std::string& text, JVal& out);
std::string jsonQuote(const std::string& s);
