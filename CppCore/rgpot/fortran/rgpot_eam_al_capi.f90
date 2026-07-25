! MIT License
! Copyright 2023--present rgpot developers

!> C entry point for the embedded-atom aluminium potential.
!!
!! Only `bind(c)` names leave this library; the physics module exports no
!! external symbols of its own that C could collide with. The neighbour
!! table lives here, saved across calls so vesin reuses its buffers, which
!! is what the legacy skin-and-rebuild bookkeeping used to do by hand.
module rgpot_eam_al_capi
   use rgpot_kinds, only: wp, ip, c_double, c_int
   use rgpot_neighbors, only: neighbor_table_t
   use rgpot_ferror, only: set_error, clear_error
   use rgpot_eam_al, only: eam_al_params_t, eam_al_energy_forces
   implicit none
   private

   public :: rgpot_eam_al_force

   type(neighbor_table_t), save :: table
   type(eam_al_params_t), save :: params

contains

   !> Evaluate embedded-atom aluminium forces and energy.
   !!
   !! `positions` and `forces` are `3 * natoms` doubles, x/y/z interleaved;
   !! `cell` is nine doubles, row-major, one cell vector per row. Energies
   !! are eV and forces eV/Angstrom. Returns zero on success, non-zero on
   !! failure with the message available through `rgpot_fortran_last_error`.
   function rgpot_eam_al_force(natoms, positions, cell, forces, energy) &
      result(status) bind(c, name="rgpot_eam_al_force")
      integer(c_int), value, intent(in) :: natoms
      real(c_double), intent(in) :: positions(3, natoms)
      real(c_double), intent(in) :: cell(3, 3)
      real(c_double), intent(out) :: forces(3, natoms)
      real(c_double), intent(out) :: energy
      integer(c_int) :: status

      integer :: build_status
      character(len=:), allocatable :: errmsg

      call clear_error()
      status = 0_c_int
      energy = 0.0_c_double
      forces = 0.0_c_double

      if (natoms < 1_c_int) return

      call eam_al_energy_forces(positions, cell, params, table, energy, &
                                forces, build_status, errmsg)
      if (build_status /= 0) then
         call set_error(errmsg)
         status = int(build_status, c_int)
      end if
   end function rgpot_eam_al_force

end module rgpot_eam_al_capi
