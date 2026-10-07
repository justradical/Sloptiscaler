// Translation-unit anchor for the OptiI18n menu translation module.
//
// The portable API and its generated dictionary are implemented inline in
// menu_i18n.h (which includes menu_zh_cn.inc). Keeping it header-only means the
// standalone behaviour suite in core/arm64ec/tests can build and link from a
// single translation unit, with no ImGui/Windows dependencies.
//
// This file exists so the module also has a compiled object in the OptiScaler
// DLL build (arm64ec/build.sh compiles every OptiScaler/**/*.cpp). Do not move
// definitions here: non-inline symbols would break the header-only guarantee the
// test relies on.

#include "menu_i18n.h"
