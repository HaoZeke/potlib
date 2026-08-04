// MIT License
// Copyright 2023--present rgpot developers

// MSVC produces no import library for a DLL that exports nothing. Export a
// version string so meson install can find rgpot.lib next to rgpot-*.dll.
#if defined(_WIN32) || defined(_WIN64)
extern "C" __declspec(dllexport) const char *rgpot_library_version(void) {
  return MESON_PROJECT_VERSION;
}
#endif
