C     nwchem_embed_legacy.F — thin NWChem F77/rtdb/ga/geom/bas calls only.
C     Called from nwchem_embed_c_api.f90 (bind(C)). Fixed-form required for
C     NWChem #include headers. MA sizes come from NWChem Memory_Defaults
C     (env / .nwchemrc), not embed-local constants.

      subroutine nwchem_legacy_init(rtdb, ok)
      implicit none
#include "mafdecls.fh"
#include "global.fh"
#include "rtdb.fh"
#include "stdio.fh"
#include "errquit.fh"
#include "cfileprefix.fh"
      integer rtdb, ok
      logical status
      character*256 rtdb_name
      integer stack, heap, global, total
      character*1 tsrc, hsrc, ssrc, gsrc
      integer g_init
      external memory_defaults, util_file_name
      external util_set_default_scratch_dir
      external util_set_default_permanent_dir
      external util_file_prefix_set, util_file_info_rtdb
      save g_init
      data g_init /0/
      ok = -1
      rtdb = -1
      if (g_init .eq. 0) then
         call pbeginf()
         call ga_initialize()
C        Same path as nwchem.F when no input "memory" directive: defaults
C        from environment / .nwchemrc / compiled-in limits.
         tsrc = 'N'
         hsrc = 'N'
         ssrc = 'N'
         gsrc = 'N'
         call memory_defaults(total, tsrc, heap, hsrc, stack,
     $        ssrc, global, gsrc)
         if (ga_uses_ma()) then
            if (.not. ma_init(mt_dbl, stack, heap + global)) return
         else
            if (.not. ma_init(mt_dbl, stack, heap)) return
         endif
C        NWChem file prefix / scratch / permanent (env: NWCHEM_SCRATCH_DIR,
C        NWCHEM_PERMANENT_DIR, or .nwchemrc; else CWD). No embed-local paths.
         call util_file_prefix_set('rgpot')
         call util_set_default_scratch_dir(scratch_dir)
         call util_set_default_permanent_dir(permanent_dir)
         g_init = 1
      endif
      if (g_init .lt. 0) return
C     RTDB in permanent/scratch via util_file_name (needs dirs above).
      call util_file_name('rgpot_embed', .true., .false., rtdb_name)
      status = rtdb_open(rtdb_name, 'empty', rtdb)
      if (.not. status) then
         rtdb = -1
         return
      endif
      call util_file_info_rtdb(rtdb)
      call util_set_rtdb_state(rtdb, .true., .false.,
     $     .false.)
      ok = 0
      return
      end

      subroutine nwchem_legacy_set_config(rtdb, basis, theory,
     $     scf_type, charge, mult, ok)
      implicit none
#include "mafdecls.fh"
#include "rtdb.fh"
      integer rtdb, charge, mult, ok
      character*(*) basis, theory, scf_type
      logical status
      double precision chg
      ok = -1
      if (rtdb .lt. 0) return
      status = rtdb_cput(rtdb, 'task:theory', 1, theory)
      status = rtdb_cput(rtdb, 'basis:name', 1, basis)
      if (theory(1:3) .eq. 'dft') then
         status = rtdb_cput(rtdb, 'dft:xc', 1, scf_type)
         status = rtdb_put(rtdb, 'dft:mult', mt_int, 1, mult)
      else
         status = rtdb_cput(rtdb, 'scf:type', 1, scf_type)
      endif
      chg = dble(charge)
      status = rtdb_put(rtdb, 'charge', mt_dbl, 1, chg)
      status = rtdb_put(rtdb, 'spin mult', mt_int, 1, mult)
      ok = 0
      return
      end

C     Direct embed: geom + library basis on RTDB, then task_energy/gradient.
C     No NWChem input files, no input_parse — library API only.
      subroutine nwchem_legacy_energy_grad(rtdb, n_atoms, pos_ang,
     $     atmnrs, basis_name, charge, mult, energy_h,
     $     grad_h_bohr, errmsg, ok)
      implicit none
#include "mafdecls.fh"
#include "global.fh"
#include "rtdb.fh"
#include "geom.fh"
#include "bas.fh"
#include "stdio.fh"
#include "errquit.fh"
#include "inp.fh"
      integer rtdb, n_atoms, charge, mult, ok
      double precision pos_ang(*)
      integer atmnrs(*)
      character*64 basis_name
      double precision energy_h
      double precision grad_h_bohr(*)
      character*(*) errmsg
      integer geom, basis, iat, n3, i
      integer l_grad, k_grad
      logical status
      double precision coords(3), qat, chg
      character*16 tag, tag_lib
      character*1024 libfile, nwtop, nwbas
      integer lnw, lbas
      character*16 vectors_in
      external task_energy, task_gradient
      logical task_energy, task_gradient
      logical bas_add_ucnt_init, bas_add_ucnt_tidy
      external bas_add_ucnt_init, bas_add_ucnt_tidy
      external bas_library_file, bas_tag_lib, util_getenv

      ok = -1
      energy_h = 0.0d0
      errmsg = ' '
      if (rtdb .lt. 0 .or. n_atoms .le. 0) then
         errmsg = 'invalid rtdb or n_atoms'
         return
      endif

C     --- geometry (geom_ncent_set + geom_cent_set) ---
      if (.not. geom_create(geom, 'geometry')) then
         errmsg = 'geom_create failed'
         return
      endif
      if (.not. geom_set_user_units(geom, 'angstrom')) then
         errmsg = 'geom_set_user_units failed'
         status = geom_destroy(geom)
         return
      endif
      if (.not. geom_ncent_set(geom, n_atoms)) then
         errmsg = 'geom_ncent_set failed'
         status = geom_destroy(geom)
         return
      endif
      do iat = 1, n_atoms
         coords(1) = pos_ang(3 * (iat - 1) + 1)
         coords(2) = pos_ang(3 * (iat - 1) + 2)
         coords(3) = pos_ang(3 * (iat - 1) + 3)
         call rgpot_z_to_elem(atmnrs(iat), tag)
         qat = dble(atmnrs(iat))
         if (.not. geom_cent_set(geom, iat, tag, coords, qat))
     $        then
            errmsg = 'geom_cent_set failed'
            status = geom_destroy(geom)
            return
         endif
      enddo
      if (.not. geom_rtdb_store(rtdb, geom, 'geometry')) then
         errmsg = 'geom_rtdb_store failed'
         status = geom_destroy(geom)
         return
      endif

C     --- AO basis from NWChem library via bas_tag_lib (no input file) ---
      if (.not. bas_create(basis, 'ao basis')) then
         errmsg = 'bas_create failed'
         status = geom_destroy(geom)
         return
      endif
      if (.not. bas_add_ucnt_init(basis)) then
         errmsg = 'bas_add_ucnt_init failed'
         status = bas_destroy(basis)
         status = geom_destroy(geom)
         return
      endif
      libfile = ' '
      call util_getenv('NWCHEM_BASIS_LIBRARY', nwbas)
      lbas = inp_strlen(nwbas)
      if (lbas .gt. 0) then
         libfile = nwbas(1:lbas)
      else
         call util_getenv('NWCHEM_TOP', nwtop)
         lnw = inp_strlen(nwtop)
         if (lnw .gt. 0) then
            libfile = nwtop(1:lnw) //
     $           '/src/basis/libraries/'
         else
            call bas_library_file(libfile)
         endif
      endif
      lbas = inp_strlen(libfile)
      if (lbas .gt. 0 .and. libfile(lbas:lbas) .ne. '/') then
         libfile(lbas + 1:lbas + 1) = '/'
      endif
      do iat = 1, n_atoms
         call rgpot_z_to_elem(atmnrs(iat), tag)
         tag_lib = tag
         call bas_tag_lib(basis, .true., tag, tag_lib,
     $        basis_name, libfile, .false., .false.)
      enddo
      if (.not. bas_add_ucnt_tidy(basis)) then
         errmsg = 'bas_add_ucnt_tidy failed'
         status = bas_destroy(basis)
         status = geom_destroy(geom)
         return
      endif
      if (.not. bas_rtdb_store(rtdb, 'ao basis', basis)) then
         errmsg = 'bas_rtdb_store failed'
         status = bas_destroy(basis)
         status = geom_destroy(geom)
         return
      endif
      status = bas_destroy(basis)
      status = geom_destroy(geom)

      chg = dble(charge)
      status = rtdb_put(rtdb, 'charge', mt_dbl, 1, chg)
      status = rtdb_put(rtdb, 'spin mult', mt_int, 1, mult)
C     SCF vector source (no input_parse defaults). hcore avoids extra
C     diagonalizations in the atomic-density guess path.
      vectors_in = 'hcore'
      status = rtdb_cput(rtdb, 'scf:input vectors', 1, vectors_in)

      if (.not. task_energy(rtdb)) then
         errmsg = 'task_energy failed'
         return
      endif
      if (.not. rtdb_get(rtdb, 'task:energy', mt_dbl, 1,
     $     energy_h)) then
         errmsg = 'rtdb_get task:energy failed'
         return
      endif

      n3 = 3 * n_atoms
      if (.not. task_gradient(rtdb)) then
         errmsg = 'task_gradient failed'
         return
      endif
      if (.not. ma_push_get(mt_dbl, n3, 'rgpot_g', l_grad,
     $     k_grad)) then
         errmsg = 'ma_push_get grad failed'
         return
      endif
      if (.not. rtdb_get(rtdb, 'task:gradient', mt_dbl, n3,
     $        dbl_mb(k_grad))) then
         do i = 1, n3
            grad_h_bohr(i) = 0.0d0
         enddo
         errmsg = 'rtdb_get task:gradient failed; grad zeroed'
      else
         do i = 1, n3
            grad_h_bohr(i) = dbl_mb(k_grad + i - 1)
         enddo
         errmsg = 'ok'
      endif
      status = ma_pop_stack(l_grad)
      ok = 0
      return
      end

      subroutine rgpot_z_to_elem(z, elem)
      implicit none
      integer z
      character*16 elem
      character*2 sym(0:36)
      data sym /'X ','H ','He','Li','Be','B ','C ','N ','O ','F ',
     $     'Ne','Na','Mg','Al','Si','P ','S ','Cl','Ar','K ','Ca',
     $     'Sc','Ti','V ','Cr','Mn','Fe','Co','Ni','Cu','Zn','Ga',
     $     'Ge','As','Se','Br','Kr'/
      if (z .ge. 0 .and. z .le. 36) then
         elem = sym(z)
      else
         elem = 'X '
      endif
      return
      end
