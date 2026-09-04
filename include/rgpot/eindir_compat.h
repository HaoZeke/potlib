#pragma once
// Layout of eindir-core's #[repr(C)] eindir_objective_t so rgpot.h can
// embed it. Matches eindir-core 0.5 (dim, low, high, eval, grad, user, free).
// Do not edit eindir-core; this is the consumer-side C view.

#ifndef EINDIR_CORE_H
#ifndef EINDIR_OBJECTIVE_T_DEFINED
#define EINDIR_OBJECTIVE_T_DEFINED

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct eindir_objective_t {
  uintptr_t dim;
  double *low;
  double *high;
  void *eval_fn;
  void *grad_fn;
  void *user_data;
  void *free_fn;
} eindir_objective_t;

#ifdef __cplusplus
}
#endif

#endif
#endif
