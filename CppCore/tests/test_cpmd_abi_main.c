/*
 * @file test_cpmd_abi_main.c
 * @brief Link against cpmd_abi_stub; verify stub reports unavailable.
 */
#include "cpmd_c_abi.h"

#include <stdio.h>
#include <string.h>

int main(void) {
  if (cpmdc_available() != 0) {
    fprintf(stderr, "cpmdc stub should report unavailable\n");
    return 1;
  }
  const char *ver = cpmdc_version();
  if (!ver || strstr(ver, "stub") == NULL) {
    fprintf(stderr, "cpmdc stub version missing stub marker\n");
    return 1;
  }
  CPMDCResult result = cpmdc_energy_gradient(0, NULL, NULL, NULL, 0, NULL);
  if (result.ok != 0 || strstr(result.message, "stub") == NULL) {
    fprintf(stderr, "cpmdc stub gradient did not fail as stub\n");
    return 1;
  }
  if (cpmdc_set_params(NULL, 0) == 0) {
    fprintf(stderr, "cpmdc stub accepted null params\n");
    return 1;
  }
  return 0;
}
