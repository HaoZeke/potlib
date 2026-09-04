#pragma once
// C ABI from rgpot-core. cbindgen emits a C header without extern "C",
// so C++ TUs must wrap the include or the symbols mangle.

#include "rgpot/eindir_compat.h"

#ifdef __cplusplus
extern "C" {
#endif
#include "rgpot.h"
#ifdef __cplusplus
}
#endif
