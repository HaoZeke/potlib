! MIT License
! Copyright 2023--present rgpot developers

!> C entry point for the Fe-He embedded-atom potential.
!!
!! Only `bind(c)` names leave this library; the physics module exports no
!! external symbols of its own that C could collide with. The neighbour
!! table lives here, saved across calls so vesin reuses its buffers.
!!
!! This potential is a mixture, so the entry point takes an atomic-number
!! array alongside the positions. Iron (26) and helium (2) are the only
!! numbers it accepts; anything else returns non-zero with a message naming
!! the offending atom.
module rgpot_fehe_capi
   use rgpot_kinds, only: wp, ip, c_double, c_int
   use rgpot_neighbors, only: neighbor_table_t
   use rgpot_ferror, only: set_error, clear_error
   use rgpot_fehe, only: fehe_params_t, fehe_energy_forces
   implicit none
   private

   public :: rgpot_fehe_force

   type(neighbor_table_t), save :: table
   type(fehe_params_t), save :: params

contains

   !> Evaluate Fe-He forces and energy.
   !!
   !! `positions` and `forces` are `3 * natoms` doubles, x/y/z interleaved;
   !! `atomic_numbers` is `natoms` ints, one per atom, in the same order;
   !! `cell` is nine doubles, row-major, one cell vector per row. Returns
   !! zero on success, non-zero on failure with the message available
   !! through `rgpot_fortran_last_error`.
   function rgpot_fehe_force(natoms, positions, atomic_numbers, cell, forces, &
                             energy) result(status) &
      bind(c, name="rgpot_fehe_force")
      integer(c_int), value, intent(in) :: natoms
      real(c_double), intent(in) :: positions(3, natoms)
      integer(c_int), intent(in) :: atomic_numbers(natoms)
      real(c_double), intent(in) :: cell(3, 3)
      real(c_double), intent(out) :: forces(3, natoms)
      real(c_double), intent(out) :: energy
      integer(c_int) :: status

      integer :: eval_status
      character(len=:), allocatable :: errmsg

      call clear_error()
      status = 0_c_int
      energy = 0.0_c_double
      forces = 0.0_c_double

      if (natoms < 1_c_int) return

      call fehe_energy_forces(positions, atomic_numbers, cell, params, table, &
                              energy, forces, eval_status, errmsg)
      if (eval_status /= 0) then
         call set_error(errmsg)
         status = int(eval_status, c_int)
      end if
   end function rgpot_fehe_force

end module rgpot_fehe_capi
