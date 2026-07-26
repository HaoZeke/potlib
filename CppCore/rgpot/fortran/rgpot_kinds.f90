! MIT License
! Copyright 2023--present rgpot developers

!> Numeric kinds shared by every Fortran potential.
!!
!! Kinds come from `iso_fortran_env`, and the C-interoperable kinds of
!! `iso_c_binding` are asserted against them at compile time: the
!! `bind(c)` entry points hand raw `double`/`int` buffers straight to the
!! kernels, so a mismatch would silently reinterpret memory. A failing
!! assertion divides by zero in a constant expression, which every
!! conforming compiler rejects.
module rgpot_kinds
   use, intrinsic :: iso_fortran_env, only: real64, int32, int64
   use, intrinsic :: iso_c_binding, only: c_double, c_int, c_size_t
   implicit none
   private

   public :: wp, ip, lp
   public :: c_double, c_int, c_size_t

   !> Working precision for every energy, force, and coordinate.
   integer, parameter :: wp = real64
   !> Index kind for atom indices crossing the C boundary.
   integer, parameter :: ip = int32
   !> Wide index kind for pair counts, which exceed 2^31 on large cells.
   integer, parameter :: lp = int64

   integer, parameter, private :: assert_wp_is_c_double = &
      1/merge(1, 0, wp == c_double)
   integer, parameter, private :: assert_ip_is_c_int = &
      1/merge(1, 0, ip == c_int)

end module rgpot_kinds
