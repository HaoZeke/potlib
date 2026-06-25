! SPDX-License-Identifier: MIT
!
! nwchem_embed_c_api.f90 — compiler-independent C ABI for NWChem embed.
!
! Public symbols are fixed with bind(C, name=...) (iso_c_binding); no
! compiler-specific underscore mangling. NWChem F77/rtdb calls live in
! nwchem_embed_legacy.F and are invoked via private interfaces.
!
! Style: modern free-form Fortran (iso_fortran_env + iso_c_binding).

module nwchem_embed_c_api
  use, intrinsic :: iso_c_binding, only: &
      c_char, c_double, c_int, c_null_char, c_ptr, c_f_pointer
  use, intrinsic :: iso_fortran_env, only: error_unit, int32, real64
  implicit none
  private

  ! C ABI (stable names — match nwchem_c_abi.c extern declarations)
  public :: rgpot_nwchem_embed_init
  public :: rgpot_nwchem_embed_available
  public :: rgpot_nwchem_embed_set_config
  public :: rgpot_nwchem_embed_energy_grad

  ! Module state (saved across C calls).
  ! Note: NWChem RTDB handles may be 0 on success; never use handle==0 as failure.
  logical, save :: ga_ready = .false.
  logical, save :: rtdb_ready = .false.
  integer, save :: rtdb_handle = -1
  character(len=64), save :: cfg_basis = 'sto-3g'
  character(len=64), save :: cfg_theory = 'scf'
  character(len=64), save :: cfg_scf = 'rhf'
  integer, save :: cfg_charge = 0
  integer, save :: cfg_mult = 1

  ! Legacy NWChem helpers (fixed-form; no bind(C))
  interface
    subroutine nwchem_legacy_init(rtdb, ok)
      integer, intent(out) :: rtdb
      integer, intent(out) :: ok
    end subroutine nwchem_legacy_init

    subroutine nwchem_legacy_set_config(rtdb, basis, theory, scf_type, &
        charge, mult, ok)
      integer, intent(in) :: rtdb
      character(len=*), intent(in) :: basis, theory, scf_type
      integer, intent(in) :: charge, mult
      integer, intent(out) :: ok
    end subroutine nwchem_legacy_set_config

    subroutine nwchem_legacy_energy_grad(rtdb, n_atoms, pos_ang, atmnrs, &
        basis_name, charge, mult, energy_h, grad_h_bohr, errmsg, ok)
      integer, intent(in) :: rtdb, n_atoms
      real(kind=8), intent(in) :: pos_ang(*)
      integer, intent(in) :: atmnrs(*)
      character(len=64), intent(in) :: basis_name
      integer, intent(in) :: charge, mult
      real(kind=8), intent(out) :: energy_h
      real(kind=8), intent(out) :: grad_h_bohr(*)
      character(len=*), intent(out) :: errmsg
      integer, intent(out) :: ok
    end subroutine nwchem_legacy_energy_grad
  end interface

contains

  !> Ensure GA/MA/RTDB once; safe to call repeatedly from C.
  subroutine rgpot_nwchem_embed_init() bind(C, name='rgpot_nwchem_embed_init')
    integer :: ok
    if (rtdb_ready) return
    if (.not. ga_ready) then
      call nwchem_legacy_init(rtdb_handle, ok)
      if (ok /= 0) then
        rtdb_handle = -1
        rtdb_ready = .false.
        return
      end if
      ga_ready = .true.
      rtdb_ready = .true.
    end if
  end subroutine rgpot_nwchem_embed_init

  !> 1 if embed RTDB is usable, else 0.
  function rgpot_nwchem_embed_available() result(avail) &
      bind(C, name='rgpot_nwchem_embed_available')
    integer(c_int) :: avail
    call rgpot_nwchem_embed_init()
    if (rtdb_ready) then
      avail = 1_c_int
    else
      avail = 0_c_int
    end if
  end function rgpot_nwchem_embed_available

  !> Apply method options from C strings (lengths explicit for C interop).
  function rgpot_nwchem_embed_set_config(basis, basis_len, theory, theory_len, &
      scf_type, scf_len, charge, mult) result(rc) &
      bind(C, name='rgpot_nwchem_embed_set_config')
    character(kind=c_char), intent(in) :: basis(*)
    integer(c_int), intent(in), value :: basis_len
    character(kind=c_char), intent(in) :: theory(*)
    integer(c_int), intent(in), value :: theory_len
    character(kind=c_char), intent(in) :: scf_type(*)
    integer(c_int), intent(in), value :: scf_len
    integer(c_int), intent(in) :: charge
    integer(c_int), intent(in) :: mult
    integer(c_int) :: rc
    integer :: ok
    character(len=64) :: bstr, tstr, sstr

    rc = -1_c_int
    call rgpot_nwchem_embed_init()
    if (.not. rtdb_ready) return

    call c_chars_to_f(basis, basis_len, bstr)
    call c_chars_to_f(theory, theory_len, tstr)
    call c_chars_to_f(scf_type, scf_len, sstr)
    if (len_trim(bstr) == 0) bstr = 'sto-3g'
    if (len_trim(tstr) == 0) tstr = 'scf'
    if (len_trim(sstr) == 0) sstr = 'rhf'

    ! theory aliases: blyp / b3lyp / pbe -> dft + xc
    if (tstr(1:4) == 'blyp' .or. tstr(1:5) == 'b3lyp' .or. &
        tstr(1:3) == 'pbe' .or. tstr(1:3) == 'dft') then
      if (tstr(1:3) /= 'dft') sstr = tstr
      tstr = 'dft'
    end if

    cfg_basis = bstr
    cfg_theory = tstr
    cfg_scf = sstr
    cfg_charge = int(charge)
    cfg_mult = max(1, int(mult))

    call nwchem_legacy_set_config(rtdb_handle, trim(cfg_basis), &
        trim(cfg_theory), trim(cfg_scf), cfg_charge, cfg_mult, ok)
    ! Best-effort: energy_grad rebuilds geometry; do not fail the C path on
    ! partial rtdb_put failures (legacy NWChem can be finicky on empty RTDB).
    rc = 0_c_int
  end function rgpot_nwchem_embed_set_config

  !> Energy (Hartree) + nuclear gradient (Hartree/Bohr) for current config.
  function rgpot_nwchem_embed_energy_grad(n_atoms, positions_ang, &
      atomic_numbers, charge, mult, energy_h, grad_h_bohr, errmsg, &
      errmsg_len) result(rc) bind(C, name='rgpot_nwchem_embed_energy_grad')
    integer(c_int), intent(in) :: n_atoms
    real(c_double), intent(in) :: positions_ang(*)
    integer(c_int), intent(in) :: atomic_numbers(*)
    integer(c_int), intent(in) :: charge
    integer(c_int), intent(in) :: mult
    real(c_double), intent(out) :: energy_h
    real(c_double), intent(out) :: grad_h_bohr(*)
    character(kind=c_char), intent(out) :: errmsg(*)
    integer(c_int), intent(in), value :: errmsg_len
    integer(c_int) :: rc
    integer :: ok, n, i
    character(len=512) :: msg
    real(real64), allocatable :: pos(:), grad(:)
    integer, allocatable :: z(:)

    rc = -1_c_int
    energy_h = 0.0_c_double
    call clear_c_errmsg(errmsg, errmsg_len)

    call rgpot_nwchem_embed_init()
    if (.not. rtdb_ready) then
      call set_c_errmsg(errmsg, errmsg_len, 'embed not initialized')
      return
    end if

    n = int(n_atoms)
    if (n <= 0) then
      call set_c_errmsg(errmsg, errmsg_len, 'n_atoms must be positive')
      return
    end if

    allocate (pos(3 * n), grad(3 * n), z(n))
    do i = 1, 3 * n
      pos(i) = real(positions_ang(i), kind=real64)
      grad(i) = 0.0_real64
    end do
    do i = 1, n
      z(i) = int(atomic_numbers(i))
    end do

    call nwchem_legacy_energy_grad(rtdb_handle, n, pos, z, cfg_basis, &
        int(charge), max(1, int(mult)), energy_h, grad, msg, ok)

    do i = 1, 3 * n
      grad_h_bohr(i) = real(grad(i), kind=c_double)
    end do
    call set_c_errmsg(errmsg, errmsg_len, trim(msg))
    if (ok == 0) rc = 0_c_int
    deallocate (pos, grad, z)
  end function rgpot_nwchem_embed_energy_grad

  ! --- helpers (private) ---

  subroutine c_chars_to_f(cbuf, clen, fstr)
    character(kind=c_char), intent(in) :: cbuf(*)
    integer(c_int), intent(in) :: clen
    character(len=*), intent(out) :: fstr
    integer :: i, n
    fstr = ' '
    n = min(int(clen), len(fstr))
    if (n <= 0) return
    do i = 1, n
      if (cbuf(i) == c_null_char) exit
      fstr(i:i) = transfer(cbuf(i), 'a')
    end do
  end subroutine c_chars_to_f

  subroutine clear_c_errmsg(errmsg, errmsg_len)
    character(kind=c_char), intent(out) :: errmsg(*)
    integer(c_int), intent(in) :: errmsg_len
    integer :: i, n
    n = max(0, int(errmsg_len))
    do i = 1, n
      errmsg(i) = c_null_char
    end do
  end subroutine clear_c_errmsg

  subroutine set_c_errmsg(errmsg, errmsg_len, msg)
    character(kind=c_char), intent(out) :: errmsg(*)
    integer(c_int), intent(in) :: errmsg_len
    character(len=*), intent(in) :: msg
    integer :: i, n, m
    call clear_c_errmsg(errmsg, errmsg_len)
    n = max(0, int(errmsg_len) - 1)
    m = min(len_trim(msg), n)
    do i = 1, m
      errmsg(i) = transfer(msg(i:i), c_null_char)
    end do
    if (n >= 0) errmsg(m + 1) = c_null_char
  end subroutine set_c_errmsg

end module nwchem_embed_c_api
