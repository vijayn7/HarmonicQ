#include <nlohmann/json.hpp>

#include <iostream>

int main() {
    const nlohmann::json config = {
        {"name", "harmonicq_analyze"},
        {"status", "ready"}
    };

    std::cout << config.dump() << '\n';
    return 0;
}
