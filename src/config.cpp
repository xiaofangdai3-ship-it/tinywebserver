#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <cstdlib>

class Config {
public:
    static Config& getInstance() {
        static Config instance;
        return instance;
    }

    bool load(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "[Config] Failed to open " << filename << ", using defaults\n";
            return false;
        }

        std::string line;
        while (std::getline(file, line)) {

            size_t start = line.find_first_not_of(" \t");
            if (start == std::string::npos) continue;
            if (line[start] == '#') continue;

            size_t colon = line.find(':');
            if (colon == std::string::npos) continue;

            std::string key = trim(line.substr(0, colon));
            std::string value = trim(line.substr(colon + 1));

            if (value.size() >= 2 && value.front() == '__STR_3__') {
                value = value.substr(1, value.size() - 2);
            }

            data_[key] = value;
        }

        std::cout << "[Config] Loaded from " << filename << "\n";
        return true;
    }

    int getInt(const std::string& key, int defaultVal) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
                return defaultVal;
            }
        }
        return defaultVal;
    }

    std::string getString(const std::string& key, const std::string& defaultVal) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            return it->second;
        }
        return defaultVal;
    }

    bool getBool(const std::string& key, bool defaultVal) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            std::string v = it->second;
            if (v == "true" || v == "1" || v == "yes" || v == "on") return true;
            if (v == "false" || v == "0" || v == "no" || v == "off") return false;
        }
        return defaultVal;
    }

    void print() const {
        std::cout << "===== Config =====\n";
        for (const auto& kv : data_) {
            std::cout << "  " << kv.first << " = " << kv.second << "\n";
        }
        std::cout << "==================\n";
    }

private:
    Config() {}
    ~Config() {}
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    std::map<std::string, std::string> data_;

    std::string trim(const std::string& s) {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }
};
