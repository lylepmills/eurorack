// Copyright 2026 Lyle Mills.
// SPDX-License-Identifier: MIT
// Exercise the actual Rack module's JSON round trip without creating a window.
#include "../src/plugin.cpp"
#include <cstdio>

int main() {
  int failures = 0, checks = 0;
  auto check = [&](bool ok, const char* label) {
    ++checks;
    if (!ok) ++failures;
    std::printf("%s %s\n", ok ? "ok   " : "FAIL ", label);
  };
  PlaitsLabEngine module;
  json_t* saved = module.dataToJson();
  check(std::string(json_string_value(json_object_get(saved, "engineId"))) == PLAITS_LAB_ENGINE_ID,
        "new patches save the compiled engine ID");
  module.dataFromJson(saved);
  check(!module.engineChanged(), "same-engine patch restores without warning");
  json_object_set_new(saved, "engineId", json_string("test/other-engine"));
  json_object_set_new(saved, "engineName", json_string("Other engine"));
  module.dataFromJson(saved);
  check(module.engineChanged(), "different-engine patch requests acknowledgement");
  json_t* again = module.dataToJson();
  module.dataFromJson(again);
  check(module.engineChanged(), "saving does not erase an unacknowledged mismatch");
  module.acceptCurrentEngine();
  check(!module.engineChanged(), "explicit acknowledgement adopts the current engine");
  json_t* legacy = json_object();
  module.dataFromJson(legacy);
  check(!module.engineChanged(), "legacy patches without identity still load");
  json_object_set_new(legacy, "engineId", json_integer(42));
  module.dataFromJson(legacy);
  check(!module.engineChanged(), "malformed identity is ignored safely");
  check(module.getParamQuantity(PlaitsLabEngine::MACRO_PARAM)->name.find("Twist") == 0,
        "fourth-control tooltip identifies TWIST");
  json_decref(saved);
  json_decref(again);
  json_decref(legacy);
  std::printf("Rack state: %d/%d passed\n", checks - failures, checks);
  return failures ? 1 : 0;
}
