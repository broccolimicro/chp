#pragma once

#include <string>
#include <filesystem>

#include <chp/graph.h>
#include <flow/func.h>

extern const std::filesystem::path SRC_DIR;
extern const std::filesystem::path BUILD_DIR;

string readStringFromFile(std::string path, bool stripNewlines=false);
chp::graph importCHPFromString(std::string name, std::string source, bool debug=false);
chp::graph importCHPFromCogString(std::string name, std::string source, bool debug=false);
void expectProperNesting(const chp::graph &g, bool debug=false);
void expectEq(const flow::Func &real, const flow::Func &expected);

