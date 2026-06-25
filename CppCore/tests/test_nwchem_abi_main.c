/**
 * @file test_nwchem_abi_main.c
 * @brief Link against nwchem_abi_stub; verify stub reports unavailable.
 */
#include "nwchem_c_abi.h"

#include <stdio.h>
#include <string.h>

int main(void) {
  if (rgpot_nwchem_abi_available() != 0) {
    fprintf(stderr, "FAIL: stub should report abi_available=0\n");
    return 1;
  }
  const char *ver = rgpot_nwchem_engine_version();
  if (!ver || strstr(ver, "stub") == NULL) {
    fprintf(stderr, "FAIL: expected stub version string, got '%s'\n",
            ver ? ver : "(null)");
    return 1;
  }
  RgpotNWChemResult r = rgpot_nwchem_energy_grad(0, NULL, NULL, 0, 1, NULL,
                                                 NULL, NULL, NULL);
  if (r.ok != 0) {
    fprintf(stderr, "FAIL: stub energy_grad should set ok=0\n");
    return 1;
  }
  if (rgpot_nwchem_set_config("sto-3g", "scf", "rhf", 0, 1) == 0) {
    fprintf(stderr, "FAIL: stub set_config should fail\n");
    return 1;
  }
  printf("ok: stub ABI version=%s\n", ver);
  return 0;
}
