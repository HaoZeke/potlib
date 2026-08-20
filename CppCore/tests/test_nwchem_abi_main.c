/**
 * @file test_nwchem_abi_main.c
 * @brief Link against nwchem_abi_stub; verify stub reports unavailable.
 */
#include "nwchem_c_abi.h"

#include <stdio.h>
#include <string.h>

int main(void) {
  if (nwchemc_abi_version() != RGPOT_NWCHEMC_ABI_VERSION) {
    fprintf(stderr, "FAIL: unexpected nwchemc ABI version\n");
    return 1;
  }
  if (nwchemc_available() != 0) {
    fprintf(stderr, "FAIL: stub should report available=0\n");
    return 1;
  }
  const char *ver = nwchemc_version();
  if (!ver || strstr(ver, "stub") == NULL) {
    fprintf(stderr, "FAIL: expected stub version string, got '%s'\n",
            ver ? ver : "(null)");
    return 1;
  }
  NWChemCResult r =
      nwchemc_energy_gradient(0, NULL, NULL, NULL, 0, NULL);
  if (r.ok != 0) {
    fprintf(stderr, "FAIL: stub energy_gradient should set ok=0\n");
    return 1;
  }
  if (nwchemc_set_params(NULL, 0) == 0) {
    fprintf(stderr, "FAIL: stub set_params should fail\n");
    return 1;
  }
  printf("ok: stub ABI version=%s\n", ver);
  return 0;
}
