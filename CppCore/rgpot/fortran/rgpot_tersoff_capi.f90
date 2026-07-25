! MIT License
! Copyright 2023--present rgpot developers

!> C entry point for the Tersoff potential.
!!
!! Only `bind(c)` names leave this library; the physics module exports no
!! external symbols of its own that C could collide with. The neighbour
!! table lives here, saved across calls so vesin reuses its buffers.
module rgpot_tersoff_capi
   use rgpot_kinds, only: wp, ip, c_double, c_int
   use rgpot_neighbors, only: neighbor_table_t
   use rgpot_ferror, only: set_error, clear_error
   use rgpot_tersoff, only: tersoff_params_t, tersoff_energy_forces
   implicit none
   private

   public :: rgpot_tersoff_force

   type(neighbor_table_t), save :: table
   type(tersoff_params_t), save :: params

contains

   !> Evaluate Tersoff forces and energy.
   !!
   !! `positions` and `forces` are `3 * natoms` doubles, x/y/z interleaved;
   !! `cell` is nine doubles, row-major, one cell vector per row. Returns
   !! zero on success, non-zero on failure with the message available
   !! through `rgpot_fortran_last_error`.
   function rgpot_tersoff_force(natoms, positions, cell, forces, energy) &
      result(status) bind(c, name="rgpot_tersoff_force")
      integer(c_int), value, intent(in) :: natoms
      real(c_double), intent(in) :: positions(3, natoms)
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

      call tersoff_energy_forces(positions, cell, params, table, energy, &
                                 forces, eval_status, errmsg)
      if (eval_status /= 0) then
         call set_error(errmsg)
         status = int(eval_status, c_int)
      end if
   end function rgpot_tersoff_force

end module rgpot_tersoff_capi
