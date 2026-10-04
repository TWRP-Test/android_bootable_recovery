#include "twrp_console_backend.h"

#include <fstream>
#include <iterator>
#include <mutex>
#include <string>
#include <unordered_map>

#include "gui/objects.hpp"
#include "gui/pages.hpp"
#include "gui/rapidxml.hpp"

namespace gui2_backend {
namespace {

console_severity severity_from_color(const std::string& color) {
  if (color == "error") return console_severity::ERROR;
  if (color == "warning") return console_severity::WARNING;
  if (color == "highlight") return console_severity::HIGHLIGHT;
  return console_severity::NORMAL;
}

// The <string> entries of a legacy language file, which is all the console
// needs of it; its font overrides belong to the legacy font stack.
void load_language_strings(const std::string& language,
                           std::unordered_map<std::string, std::string>* strings) {
  std::ifstream file("/twres/customlanguages/" + language + ".xml", std::ios::binary);
  if (!file) file.open("/twres/languages/" + language + ".xml", std::ios::binary);
  if (!file) return;
  std::string buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
  rapidxml::xml_document<> lang;
  lang.parse<0>(&buffer[0]);
  rapidxml::xml_node<>* parent = lang.first_node("language");
  rapidxml::xml_node<>* resources = parent == nullptr ? nullptr : parent->first_node("resources");
  if (resources == nullptr) return;
  for (rapidxml::xml_node<>* child = resources->first_node("string"); child != nullptr;
       child = child->next_sibling("string")) {
    rapidxml::xml_attribute<>* name = child->first_attribute("name");
    if (name != nullptr) (*strings)[name->value()] = child->value();
  }
}

std::mutex strings_mutex;
std::unordered_map<std::string, std::string> language_strings;

}  // namespace

size_t twrp_console_backend::fetch(size_t from, std::vector<console_line>* lines) {
  std::vector<std::string> text;
  std::vector<std::string> colors;
  const size_t total = GUIConsole::Get_Lines(from, lines == nullptr ? nullptr : &text,
                                             lines == nullptr ? nullptr : &colors);
  if (lines == nullptr) return total;

  lines->reserve(lines->size() + text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    console_line line;
    line.text = std::move(text[i]);
    line.severity = severity_from_color(i < colors.size() ? colors[i] : std::string("normal"));
    lines->push_back(std::move(line));
  }
  return total;
}

// en.xml first, then the language on top, as PageManager::LoadLanguage does.
void twrp_console_backend::load_strings(const std::string& language) {
  std::unordered_map<std::string, std::string> strings;
  load_language_strings("en", &strings);
  if (language != "en") load_language_strings(language, &strings);
  std::lock_guard<std::mutex> lock(strings_mutex);
  language_strings.swap(strings);
}

std::string twrp_console_backend::translate(const std::string& key) {
  std::lock_guard<std::mutex> lock(strings_mutex);
  const auto it = language_strings.find(key);
  return it == language_strings.end() ? std::string() : it->second;
}

// setlanguage: the partition names are translated again, and the console is
// rebuilt from its messages in the new language.
void twrp_console_backend::retranslate(const std::string& language) {
  load_strings(language);
  PageManager::TranslatePartitionNames(language);
  GUIConsole::Clear_For_Retranslation();
}

}  // namespace gui2_backend
