C     nwchem_embed.F -- Minimal NWChem embed via RTDB / task_energy / gradient.
C     Built only when RGPOT_HAS_NWCHEM is defined and nwchem_root is set.
C     Pattern mirrors NWChem external embedding (NWChemWrap / task_energy).
C
C     Public C-callable entry points (trailing underscore for C linkage):
C       rgpot_nwchem_embed_init_
C       rgpot_nwchem_embed_set_config_
C       rgpot_nwchem_embed_energy_grad_
C       rgpot_nwchem_embed_available_

#include "mafdecls.fh"
#include "global.fh"
#include "rtdb.fh"
#include "geom.fh"
#include "bas.fh"
#include "sym.fh"
#include "util.fh"
#include "stdio.fh"
#include "errquit.fh"

      block data rgpot_nwchem_embed_bd
      implicit none
      integer rtdb_handle
      common /rgpot_nwchem_cmn/ rtdb_handle
      data rtdb_handle /0/
      end

      subroutine rgpot_nwchem_embed_init_()
      implicit none
#include "mafdecls.fh"
#include "global.fh"
#include "rtdb.fh"
#include "stdio.fh"
      integer rtdb_handle
      common /rgpot_nwchem_cmn/ rtdb_handle
      logical status
      character*256 rtdb_name
      external util_sgroup_mygroup
      integer util_sgroup_mygroup
C     One-time NWChem runtime init (idempotent best-effort).
      if (rtdb_handle .ne. 0) return
      call util_init(1)
      rtdb_name = 'rgpot_nwchem.db'
      status = rtdb_open(rtdb_name, 'empty', rtdb_handle)
      if (.not. status) then
         rtdb_handle = 0
      endif
      return
      end

      integer function rgpot_nwchem_embed_available_()
      implicit none
      integer rtdb_handle
      common /rgpot_nwchem_cmn/ rtdb_handle
      call rgpot_nwchem_embed_init_()
      if (rtdb_handle .ne. 0) then
         rgpot_nwchem_embed_available_ = 1
      else
         rgpot_nwchem_embed_available_ = 0
      endif
      return
      end

      integer function rgpot_nwchem_embed_set_config_(
     $     basis, basis_len, theory, theory_len, scf_type, scf_len,
     $     charge, mult)
      implicit none
#include "rtdb.fh"
      character*(*) basis, theory, scf_type
      integer basis_len, theory_len, scf_len
      integer charge, mult
      integer rtdb_handle
      common /rgpot_nwchem_cmn/ rtdb_handle
      logical status
      character*64 bstr, tstr, sstr
      external util_trim_char
      call rgpot_nwchem_embed_init_()
      rgpot_nwchem_embed_set_config_ = -1
      if (rtdb_handle .eq. 0) return
      bstr = ' '
      tstr = ' '
      sstr = ' '
      if (basis_len .gt. 0) bstr(1:min(basis_len,64)) = basis(1:min(basis_len,64))
      if (theory_len .gt. 0) tstr(1:min(theory_len,64)) = theory(1:min(theory_len,64))
      if (scf_len .gt. 0) sstr(1:min(scf_len,64)) = scf_type(1:min(scf_len,64))
      if (tstr(1:1) .eq. ' ') tstr = 'scf'
      if (bstr(1:1) .eq. ' ') bstr = 'sto-3g'
      if (sstr(1:1) .eq. ' ') sstr = 'rhf'
      status = rtdb_put(rtdb_handle, 'task:theory', mt_char, 1, tstr)
      status = rtdb_put(rtdb_handle, 'basis:name', mt_char, 1, bstr)
      status = rtdb_put(rtdb_handle, 'scf:type', mt_char, 1, sstr)
      status = rtdb_put(rtdb_handle, 'charge', mt_int, 1, charge)
      status = rtdb_put(rtdb_handle, 'spin mult', mt_int, 1, mult)
      if (status) rgpot_nwchem_embed_set_config_ = 0
      return
      end

      integer function rgpot_nwchem_embed_energy_grad_(
     $     n_atoms, positions_ang, atomic_numbers, charge, mult,
     $     energy_h, grad_h_bohr, errmsg, errmsg_len)
      implicit none
#include "mafdecls.fh"
#include "global.fh"
#include "rtdb.fh"
#include "geom.fh"
#include "bas.fh"
#include "stdio.fh"
#include "errquit.fh"
      integer n_atoms
      double precision positions_ang(*)
      integer atomic_numbers(*)
      integer charge, mult
      double precision energy_h
      double precision grad_h_bohr(*)
      character*(*) errmsg
      integer errmsg_len
      integer rtdb_handle
      common /rgpot_nwchem_cmn/ rtdb_handle
      integer geom, basis, i, j, iat
      logical status
      double precision coords(3), ang2bohr
      character*16 elem
      character*256 theory_task
      external task_energy, task_gradient
      logical task_energy, task_gradient
      integer ma_type, n3
      integer l_grad, k_grad
      double precision bohr2ang
      parameter (ang2bohr = 1.8897259886d0)
      parameter (bohr2ang = 0.52917721067d0)

      rgpot_nwchem_embed_energy_grad_ = -1
      energy_h = 0.0d0
      errmsg = ' '
      call rgpot_nwchem_embed_init_()
      if (rtdb_handle .eq. 0) then
         errmsg = 'rtdb not open'
         return
      endif
      if (n_atoms .le. 0) then
         errmsg = 'n_atoms <= 0'
         return
      endif

C     Build geometry from Z + positions (Angstrom -> Bohr for geom_cart_set)
      if (.not. geom_create(geom, 'rgpot_geom')) then
         errmsg = 'geom_create failed'
         return
      endif
      do iat = 1, n_atoms
         call rgpot_z_to_elem(atomic_numbers(iat), elem)
         coords(1) = positions_ang(3*(iat-1)+1) * ang2bohr
         coords(2) = positions_ang(3*(iat-1)+2) * ang2bohr
         coords(3) = positions_ang(3*(iat-1)+3) * ang2bohr
         if (.not. geom_cart_set(geom, iat, elem, coords,
     $        dble(atomic_numbers(iat)))) then
            errmsg = 'geom_cart_set failed'
            status = geom_destroy(geom)
            return
         endif
      enddo
      if (.not. geom_rtdb_store(rtdb_handle, geom, 'geometry')) then
         errmsg = 'geom_rtdb_store failed'
         status = geom_destroy(geom)
         return
      endif
      status = geom_destroy(geom)

      status = rtdb_put(rtdb_handle, 'charge', mt_int, 1, charge)
      status = rtdb_put(rtdb_handle, 'spin mult', mt_int, 1, mult)

C     Energy
      if (.not. task_energy(rtdb_handle)) then
         errmsg = 'task_energy failed'
         return
      endif
      if (.not. rtdb_get(rtdb_handle, 'task:energy', mt_dbl, 1,
     $     energy_h)) then
         errmsg = 'rtdb_get task:energy failed'
         return
      endif

C     Gradient (Hartree/Bohr)
      n3 = 3 * n_atoms
      if (.not. task_gradient(rtdb_handle)) then
         errmsg = 'task_gradient failed'
         return
      endif
      if (.not. rtdb_get(rtdb_handle, 'task:gradient', mt_dbl, n3,
     $     grad_h_bohr)) then
C        Some NWChem versions store under theory-specific keys; zero as fallback
         do i = 1, n3
            grad_h_bohr(i) = 0.0d0
         enddo
         errmsg = 'rtdb_get task:gradient failed (energy ok, grad zeroed)'
C        Still report energy success with warning in message path
      endif

      rgpot_nwchem_embed_energy_grad_ = 0
      return
      end

      subroutine rgpot_z_to_elem(z, elem)
      implicit none
      integer z
      character*16 elem
      character*2 sym(0:36)
      data sym /'X ','H ','He','Li','Be','B ','C ','N ','O ','F ','Ne',
     $     'Na','Mg','Al','Si','P ','S ','Cl','Ar','K ','Ca','Sc',
     $     'Ti','V ','Cr','Mn','Fe','Co','Ni','Cu','Zn','Ga','Ge',
     $     'As','Se','Br','Kr'/
      if (z .ge. 0 .and. z .le. 36) then
         elem = sym(z)
      else
         elem = 'X '
      endif
      return
      end
