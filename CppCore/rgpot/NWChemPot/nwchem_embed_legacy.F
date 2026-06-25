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
      external util_blas_set_num_threads, rtdb_init, nxtask_init
      save g_init
      data g_init /0/
      ok = -1
      rtdb = -1
      if (g_init .eq. 0) then
C        Host may already have MPI_Init (C driver / mpirun). Only start MPI
C        from Fortran when embed owns the process (matches in-process embed).
         call rgpot_mpi_start_if_needed()
C        Same path as nwchem.F when no input "memory" directive: defaults
C        from environment / .nwchemrc / compiled-in limits.
         tsrc = 'N'
         hsrc = 'N'
         ssrc = 'N'
         gsrc = 'N'
         call memory_defaults(total, tsrc, heap, hsrc, stack,
     $        ssrc, global, gsrc)
C        Match nwchem.F order: GA limited init, then MA (GA may use MA).
C        Fallback if memory_defaults yields zero (no .nwchemrc / env).
         if (stack .le. 0) stack = 4000000
         if (heap .le. 0) heap = 4000000
         if (global .le. 0) global = 8000000
C        Match NWChemWrap / geninterface embed path (not full nwchem.F).
         call ga_initialize()
         if (ga_uses_ma()) then
            if (.not. ma_init(mt_dbl, stack, heap + global)) return
         else
            if (.not. ma_init(mt_dbl, stack, heap)) return
            call ga_set_memory_limit(ma_sizeof(mt_dbl, global, mt_byte))
         endif
         call util_blas_set_num_threads(1)
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
      call geom_hnd_parallel(.true.)
      call nxtask_init(rtdb)
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

C     Direct embed: build geom+basis via NWChem embed API nw_inp_from_character
C     (same path as NWChemWrap/geninterface; internal util_file_name temp only),
C     then task_energy/gradient. No user-facing .nw files; coords from C ABI.
      subroutine nwchem_legacy_energy_grad(rtdb, n_atoms, pos_ang,
     $     atmnrs, basis_name, charge, mult, energy_h,
     $     grad_h_bohr, errmsg, ok)
      implicit none
#include "mafdecls.fh"
#include "global.fh"
#include "rtdb.fh"
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
      integer iat, n3, i, ipos, lb
      integer l_grad, k_grad
      integer maxat
      parameter (maxat = 64)
      logical status
      double precision chg
      character*16 tag
      character*16 vectors_in
      character*8192 blk
      character*128 line
      external task_energy, task_gradient
      logical task_energy, task_gradient
      external nw_inp_from_character
      logical nw_inp_from_character

      ok = -1
      energy_h = 0.0d0
      errmsg = ' '
      if (rtdb .lt. 0 .or. n_atoms .le. 0) then
         errmsg = 'invalid rtdb or n_atoms'
         return
      endif
      if (n_atoms .gt. maxat) then
         errmsg = 'n_atoms exceeds embed maxat=64'
         return
      endif

C     Geometry (Angstrom from C) + library basis — one embed directive block.
C     Proven to yield finite SCF; thin geom_cent_set path leaves incomplete
C     symmetry/mass/autoz state and NaN integrals.
      blk = 'geometry units angstrom' // char(10)
      ipos = inp_strlen(blk) + 1
      do iat = 1, n_atoms
         call rgpot_z_to_elem(atmnrs(iat), tag)
         write (line, '(1x,a2,1x,3f20.12)') tag(1:2),
     $        pos_ang(3 * (iat - 1) + 1),
     $        pos_ang(3 * (iat - 1) + 2),
     $        pos_ang(3 * (iat - 1) + 3)
         lb = inp_strlen(line)
         if (ipos + lb + 1 .gt. len(blk)) then
            errmsg = 'embed directive block overflow'
            return
         endif
         blk(ipos:ipos + lb - 1) = line(1:lb)
         ipos = ipos + lb
         blk(ipos:ipos) = char(10)
         ipos = ipos + 1
      enddo
      blk(ipos:ipos + 3) = 'end' // char(10)
      ipos = ipos + 4
      write (line, '(''basis; * library '',a,'' ; end'')')
     $     basis_name(1:inp_strlen(basis_name))
      lb = inp_strlen(line)
      if (ipos + lb + 1 .gt. len(blk)) then
         errmsg = 'embed directive block overflow (basis)'
         return
      endif
      blk(ipos:ipos + lb - 1) = line(1:lb)
      ipos = ipos + lb
      blk(ipos:ipos) = char(10)
      if (.not. nw_inp_from_character(rtdb, blk(1:ipos))) then
         errmsg = 'nw_inp_from_character geom/basis failed'
         return
      endif

      chg = dble(charge)
      status = rtdb_put(rtdb, 'charge', mt_dbl, 1, chg)
      status = rtdb_put(rtdb, 'spin mult', mt_int, 1, mult)
      vectors_in = 'atomic'
      status = rtdb_cput(rtdb, 'scf:input vectors', 1, vectors_in)
      status = rtdb_cput(rtdb, 'scf:type', 1, 'rhf')
      status = rtdb_cput(rtdb, 'task:operation', 1, 'energy')
      status = rtdb_cput(rtdb, 'task:theory', 1, 'scf')

      if (.not. task_energy(rtdb)) then
         errmsg = 'task_energy/scf failed'
         return
      endif
      if (.not. rtdb_get(rtdb, 'task:energy', mt_dbl, 1, energy_h)) then
         if (.not. rtdb_get(rtdb, 'scf:energy', mt_dbl, 1,
     $        energy_h)) then
            errmsg = 'rtdb_get task/scf energy failed'
            return
         endif
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
C     Left-align element symbol in character*16 (matches geom_input tags).
      elem = ' '
      if (z .ge. 0 .and. z .le. 36) then
         elem(1:2) = sym(z)
      else
         elem(1:2) = 'X '
      endif
      return
      end

C     Call pbeginf only when MPI is not already initialized (C/MPI host).
C     Open MPI Fortran bindings use default INTEGER (not necessarily
C     -fdefault-integer-8); force integer*4 for mpi_initialized args.
      subroutine rgpot_mpi_start_if_needed()
      implicit none
      integer*4 ierr4, flag4
      external mpi_initialized, pbeginf
      flag4 = 0
      ierr4 = 0
      call mpi_initialized(flag4, ierr4)
      if (flag4 .eq. 0) then
         call pbeginf()
      endif
      return
      end
