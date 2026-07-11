#include "config/config.h"
#include "core/engine/engine.h"
#include "ofl/lexer.h"
#include "ofl/parser.h"
#include "ofl/resolver.h"
#include "ofl/validator.h"
#include "ofl/builder.h"
#include <fstream>
#include <sstream>
#include <thread>

#include "visualizer/window.h"

int main() {
    Config cfg = load_config();
    luminary::core::Engine engine(cfg.engine);

    std::ifstream file("fixtures/robin600e.ofl");
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string source = ss.str();

    ofl::Lexer lexer(source);
    auto tokens = lexer.tokenize();
    ofl::Parser parser(tokens, "robin600e.ofl");
    auto node = parser.parse();

    ofl::Resolver resolver({std::filesystem::path("fixtures")});
    resolver.resolve(node);

    ofl::Validator validator;
    validator.validate(node);

    auto fixture = std::make_shared<ofl::Fixture>(ofl::Builder::build(node));
    engine.patch().add(fixture, 0, DmxAddress(1));
    engine.start();

    engine.stop();
}
