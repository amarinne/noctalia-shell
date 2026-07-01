#include "theme/builtin_templates.h"

#include "core/process.h"
#include "core/resource_paths.h"
#include "core/toml.h" // IWYU pragma: keep
#include "util/string_utils.h"

#include <algorithm>
#include <chrono>
#include <filesystem>

namespace noctalia::theme {

  std::vector<BuiltinTemplateInfo> loadBuiltinTemplateInfo(std::string* err) {
    const std::filesystem::path configPath = paths::assetPath("templates/builtin.toml");
    toml::table root;
    try {
      root = toml::parse_file(configPath.string());
    } catch (const toml::parse_error& e) {
      if (err != nullptr) {
        *err = e.description();
      }
      return {};
    }

    std::vector<BuiltinTemplateInfo> out;
    const toml::table* catalog = root["catalog"].as_table();
    if (catalog == nullptr) {
      return out;
    }

    for (const auto& [idNode, node] : *catalog) {
      const toml::table* info = node.as_table();
      if (info == nullptr) {
        continue;
      }
      BuiltinTemplateInfo entry;
      entry.id = std::string(idNode.str());
      if (const auto name = info->get_as<std::string>("name")) {
        entry.name = name->get();
      }
      if (const auto category = info->get_as<std::string>("category")) {
        entry.category = category->get();
      }
      out.push_back(std::move(entry));
    }

    const toml::table* templates = root["templates"].as_table();
    if (templates != nullptr) {
      for (auto& entry : out) {
        const toml::table* tpl = templates->get_as<toml::table>(entry.id);
        if (tpl == nullptr) {
          continue;
        }
        if (const auto opd = tpl->get_as<std::string>("output_path_dynamic")) {
          entry.outputDynamic = true;
          entry.outputPathDynamicCommand = opd->get();
        }
        const toml::node* op = tpl->get("output_path");
        if (op != nullptr) {
          if (const auto str = op->as_string()) {
            entry.outputPaths.push_back(str->get());
          } else if (const auto arr = op->as_array()) {
            for (const auto& item : *arr) {
              if (const auto itemStr = item.as_string()) {
                entry.outputPaths.push_back(itemStr->get());
              }
            }
          }
        }
      }
    }

    if (configPath.has_parent_path()) {
      const std::string configDir = configPath.parent_path().string();
      for (auto& entry : out) {
        if (!entry.outputDynamic || entry.outputPathDynamicCommand.empty()) {
          continue;
        }
        static constexpr std::string_view kConfigDirToken = "{{ config_dir }}";
        std::string cmd = entry.outputPathDynamicCommand;
        for (std::size_t pos = 0; (pos = cmd.find(kConfigDirToken, pos)) != std::string::npos;
             pos += configDir.size()) {
          cmd.replace(pos, kConfigDirToken.size(), configDir);
        }
        process::RunOptions opts;
        opts.timeout = std::chrono::seconds(5);
        const auto result = process::runSync({"/bin/sh", "-lc", cmd}, opts);
        if (result && !result.out.empty()) {
          std::string_view remaining(result.out);
          while (!remaining.empty()) {
            const std::size_t nl = remaining.find('\n');
            const std::string_view line = nl == std::string_view::npos ? remaining : remaining.substr(0, nl);
            remaining = nl == std::string_view::npos ? std::string_view{} : remaining.substr(nl + 1);
            std::string trimmed = StringUtils::trim(line);
            if (trimmed.empty() || trimmed.front() == '#') {
              continue;
            }
            entry.outputPaths.push_back(std::move(trimmed));
          }
          entry.outputDynamic = false;
        }
      }
    }

    std::ranges::sort(out, [](const BuiltinTemplateInfo& lhs, const BuiltinTemplateInfo& rhs) {
      if (lhs.category != rhs.category) {
        return lhs.category < rhs.category;
      }
      return lhs.id < rhs.id;
    });
    return out;
  }

  std::vector<AvailableTemplate> availableTemplates() {
    auto entries = loadBuiltinTemplateInfo();
    std::vector<AvailableTemplate> out;
    out.reserve(entries.size());
    for (auto& entry : entries) {
      AvailableTemplate t;
      t.id = std::move(entry.id);
      t.displayName = entry.name.empty() ? t.id : std::move(entry.name);
      t.category = std::move(entry.category);
      t.outputPaths = std::move(entry.outputPaths);
      t.outputDynamic = entry.outputDynamic;
      out.push_back(std::move(t));
    }
    std::ranges::sort(out, [](const AvailableTemplate& a, const AvailableTemplate& b) {
      if (a.displayName != b.displayName) {
        return a.displayName < b.displayName;
      }
      return a.id < b.id;
    });
    out.erase(
        std::ranges::unique(
            out, [](const AvailableTemplate& a, const AvailableTemplate& b) { return a.id == b.id; }
        ).begin(),
        out.end()
    );
    return out;
  }

} // namespace noctalia::theme
