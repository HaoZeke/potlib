! MIT License
! Copyright 2023--present rgpot developers
!
! Lenosky silicon. The kernel descends from eOn
! (https://github.com/TheochemUI/eOn, client/potentials/Lenosky),
! BSD-3-Clause, copyright the eOn Development Team. The physics is
! T. Lenosky et al., Modelling Simul. Mater. Sci. Eng. 8, 825 (2000), in
! the linear-scaling arrangement S. Goedecker published in 2002.

!> Lenosky silicon in gather form.
!!
!! Lenosky is a spline potential: five cubic splines on uniform grids carry
!! the whole model, and atom `i` gets
!!
!!   `E_i = sum_j phi(r_ij)/2 + U(n_i)`,
!!   `n_i = sum_j rho(r_ij) + sum_{j<k} f(r_ij) f(r_ik) g(cos theta_jik)`
!!
!! so the embedding density `n_i` plays the part EDIP's coordination plays:
!! one scalar per atom through which every neighbour of `i` feels every
!! other neighbour of `i`. The published arrangement evaluates `n_i`, then
!! walks the neighbourhood a second time scattering `U'(n_i)` times each
!! stored partial derivative into three atoms' force slots.
!!
!! The gather closes on the same observation as `rgpot_edip`: once `U'(n)`
!! is known for every atom, the force on `i` is a sum over `i`'s own
!! neighbourhood,
!!
!!   * pair, `-phi'(r_ij) * w_ij`, both halves collected at once;
!!   * two-body embedding, `-[U'(n_i) + U'(n_j)] * rho'(r_ij) * w_ij`;
!!   * three-body embedding, `i` as the centre of `(j, i, k)` and as an end
!!     of every triplet centred on a neighbour `m`, the two roles of
!!     `rgpot_sw`, weighted by `U'` of the centre in each case.
!!
!! Evaluation is therefore two `do concurrent` passes: one over the
!! per-atom scalars (energy and `U'(n)`), one over forces. Neither writes
!! outside the atom it is indexed by.
!!
!! `w_ij` points from the leg towards the centre, `(r_i - r_j)/r_ij`, which
!! is the sign convention of the legacy `rel` array and the reverse of the
!! neighbour table's stored vector.
!!
!! A triplet whose two ends are the same atom reached through different
!! periodic images is dropped, in the centre role and the end role alike,
!! as the legacy `kat < jat` test drops it, so the force stays the exact
!! gradient of the energy reported here.
module rgpot_lenosky
   use rgpot_kinds, only: wp, ip
   use rgpot_neighbors, only: neighbor_table_t
   implicit none
   private

   public :: lenosky_params_t, lenosky_energy_forces

   !> Knots in the longest of the five tables; shorter tables pad and carry
   !! their own extent in `spline_t%n`.
   integer, parameter :: max_knots = 11

   ! Pair energy phi(r), eV, on [1.5, 4.5] Angstrom.
   real(wp), parameter :: cof_phi(0:max_knots - 1) = reshape([ &
                          0.69299400000000e+01_wp, -0.43995000000000e+00_wp, &
                          -0.17012300000000e+01_wp, -0.16247300000000e+01_wp, &
                          -0.99696000000000e+00_wp, -0.27391000000000e+00_wp, &
                          -0.24990000000000e-01_wp, -0.17840000000000e-01_wp, &
                          -0.96100000000000e-02_wp, 0.00000000000000e+00_wp], &
                                                   [max_knots], pad=[0.0_wp])
   real(wp), parameter :: dof_phi(0:max_knots - 1) = reshape([ &
                          0.16533229480429e+03_wp, 0.39415410391417e+02_wp, &
                          0.68710036300407e+01_wp, 0.53406950884203e+01_wp, &
                          0.15347960162782e+01_wp, -0.63347591535331e+01_wp, &
                          -0.17987794021458e+01_wp, 0.47429676211617e+00_wp, &
                          -0.40087646318907e-01_wp, -0.23942617684055e+00_wp], &
                                                   [max_knots], pad=[0.0_wp])

   ! Two-body embedding density rho(r) on [1.5, 3.5] Angstrom.
   real(wp), parameter :: cof_rho(0:max_knots - 1) = [ &
                          0.13747000000000e+00_wp, -0.14831000000000e+00_wp, &
                          -0.55972000000000e+00_wp, -0.73110000000000e+00_wp, &
                          -0.76283000000000e+00_wp, -0.72918000000000e+00_wp, &
                          -0.66620000000000e+00_wp, -0.57328000000000e+00_wp, &
                          -0.40690000000000e+00_wp, -0.16662000000000e+00_wp, &
                          0.00000000000000e+00_wp]
   real(wp), parameter :: dof_rho(0:max_knots - 1) = [ &
                          -0.32275496741918e+01_wp, -0.64119006516165e+01_wp, &
                          0.10030652280658e+02_wp, 0.22937915289857e+01_wp, &
                          0.17416816033995e+01_wp, 0.54648205741626e+00_wp, &
                          0.47189016693543e+00_wp, 0.20569572748420e+01_wp, &
                          0.23192807336964e+01_wp, -0.24908020962757e+00_wp, &
                          -0.12371959895186e+02_wp]

   ! Three-body radial weight f(r) on [1.5, 3.5] Angstrom.
   real(wp), parameter :: cof_fff(0:max_knots - 1) = reshape([ &
                          0.12503100000000e+01_wp, 0.86821000000000e+00_wp, &
                          0.60846000000000e+00_wp, 0.48756000000000e+00_wp, &
                          0.44163000000000e+00_wp, 0.37610000000000e+00_wp, &
                          0.27145000000000e+00_wp, 0.14814000000000e+00_wp, &
                          0.48550000000000e-01_wp, 0.00000000000000e+00_wp], &
                                                   [max_knots], pad=[0.0_wp])
   real(wp), parameter :: dof_fff(0:max_knots - 1) = reshape([ &
                          0.27904652711432e+02_wp, -0.45230754228635e+01_wp, &
                          0.50531739800222e+01_wp, 0.11806545027747e+01_wp, &
                          -0.66693699112098e+00_wp, -0.89430653829079e+00_wp, &
                          -0.50891685571587e+00_wp, 0.66278396115427e+00_wp, &
                          0.73976101109878e+00_wp, 0.25795319944506e+01_wp], &
                                                   [max_knots], pad=[0.0_wp])

   ! Embedding energy U(n), eV, on [-1.770930, 7.908520].
   real(wp), parameter :: cof_uuu(0:max_knots - 1) = reshape([ &
                          -0.10749300000000e+01_wp, -0.20045000000000e+00_wp, &
                          0.41422000000000e+00_wp, 0.87939000000000e+00_wp, &
                          0.12668900000000e+01_wp, 0.16299800000000e+01_wp, &
                          0.19773800000000e+01_wp, 0.23961800000000e+01_wp], &
                                                   [max_knots], pad=[0.0_wp])
   real(wp), parameter :: dof_uuu(0:max_knots - 1) = reshape([ &
                          -0.14827125747284e+00_wp, -0.14922155328475e+00_wp, &
                          -0.70113224223509e-01_wp, -0.39449020349230e-01_wp, &
                          -0.15815242579643e-01_wp, 0.26112640061855e-01_wp, &
                          -0.13786974745095e+00_wp, 0.74941595372657e+00_wp], &
                                                   [max_knots], pad=[0.0_wp])

   ! Three-body angular weight g(cos theta) on [-1.0, 0.80014].
   real(wp), parameter :: cof_ggg(0:max_knots - 1) = reshape([ &
                          0.52541600000000e+01_wp, 0.23591500000000e+01_wp, &
                          0.11959500000000e+01_wp, 0.12299500000000e+01_wp, &
                          0.20356500000000e+01_wp, 0.34247400000000e+01_wp, &
                          0.49485900000000e+01_wp, 0.56179900000000e+01_wp], &
                                                   [max_knots], pad=[0.0_wp])
   real(wp), parameter :: dof_ggg(0:max_knots - 1) = reshape([ &
                          0.15826876132396e+02_wp, 0.31176239377907e+02_wp, &
                          0.16589446539683e+02_wp, 0.11083892500520e+02_wp, &
                          0.90887216383860e+01_wp, 0.54902279653967e+01_wp, &
                          -0.18823313223755e+02_wp, -0.77183416481005e+01_wp], &
                                                   [max_knots], pad=[0.0_wp])

   !> One cubic spline on a uniform grid: `n` knots spanning `[tmin, tmax]`,
   !! values in `cof` and second derivatives in `dof`.
   !!
   !! `hi` is the inverse knot spacing, `hsixth` is `h/6` and `h2sixth` is
   !! `h*h/6`, all carried as the legacy tabulated them rather than derived,
   !! so the arithmetic is bit-for-bit the arithmetic Lenosky's tables were
   !! fitted against.
   type :: spline_t
      integer :: n = 0
      real(wp) :: tmin = 0.0_wp
      real(wp) :: tmax = 0.0_wp
      real(wp) :: hi = 0.0_wp
      real(wp) :: hsixth = 0.0_wp
      real(wp) :: h2sixth = 0.0_wp
      real(wp) :: cof(0:max_knots - 1) = 0.0_wp
      real(wp) :: dof(0:max_knots - 1) = 0.0_wp
   end type spline_t

   !> Lenosky parameters: the five splines. Defaults are the silicon tables
   !! hard-coded in the eOn kernel.
   type :: lenosky_params_t
      !> Pair energy.
      type(spline_t) :: phi = spline_t(n=10, tmin=0.1500000e+01_wp, &
                                       tmax=0.4500000e+01_wp, &
                                       hi=3.00000000000_wp, &
                                       hsixth=5.55555555555556e-2_wp, &
                                       h2sixth=1.85185185185185e-2_wp, &
                                       cof=cof_phi, dof=dof_phi)
      !> Two-body embedding density.
      type(spline_t) :: rho = spline_t(n=11, tmin=0.1500000e+01_wp, &
                                       tmax=0.3500000e+01_wp, &
                                       hi=5.00000000000_wp, &
                                       hsixth=3.33333333333333e-2_wp, &
                                       h2sixth=6.66666666666667e-3_wp, &
                                       cof=cof_rho, dof=dof_rho)
      !> Three-body radial weight.
      type(spline_t) :: fff = spline_t(n=10, tmin=0.1500000e+01_wp, &
                                       tmax=0.3500000e+01_wp, &
                                       hi=4.50000000000_wp, &
                                       hsixth=3.70370370370370e-2_wp, &
                                       h2sixth=8.23045267489712e-3_wp, &
                                       cof=cof_fff, dof=dof_fff)
      !> Embedding energy.
      type(spline_t) :: uuu = spline_t(n=8, tmin=-0.1770930e+01_wp, &
                                       tmax=0.7908520e+01_wp, &
                                       hi=0.723181585730594_wp, &
                                       hsixth=0.230463095238095_wp, &
                                       h2sixth=0.318679429600340_wp, &
                                       cof=cof_uuu, dof=dof_uuu)
      !> Three-body angular weight.
      type(spline_t) :: ggg = spline_t(n=8, tmin=-0.1000000e+01_wp, &
                                       tmax=0.8001400e+00_wp, &
                                       hi=3.88858644327663_wp, &
                                       hsixth=4.28604761904762e-2_wp, &
                                       h2sixth=1.10221225156463e-2_wp, &
                                       cof=cof_ggg, dof=dof_ggg)
   contains
      procedure :: cutoff => lenosky_cutoff
   end type lenosky_params_t

contains

   !> Interaction cutoff: the widest upper knot among the radial tables.
   pure function lenosky_cutoff(self) result(rcut)
      class(lenosky_params_t), intent(in) :: self
      real(wp) :: rcut

      rcut = max(self%phi%tmax, max(self%rho%tmax, self%fff%tmax))
   end function lenosky_cutoff

   !> Spline value `y` and slope `yp` at `x`.
   !!
   !! Outside `[tmin, tmax]` the spline continues linearly with its end
   !! slope. The knot index is held one below the last interval so that
   !! `x == tmax` lands on the final interval rather than one past the
   !! table; the interpolation weights then pick out the last knot exactly.
   pure subroutine spline_eval(sp, x, y, yp)
      type(spline_t), intent(in) :: sp
      real(wp), intent(in) :: x
      real(wp), intent(out) :: y, yp

      integer :: klo, khi
      real(wp) :: tt, a, b, a2, b2

      if (x < sp%tmin) then
         yp = sp%hi*(sp%cof(1) - sp%cof(0)) &
              - (sp%dof(1) + 2.0_wp*sp%dof(0))*sp%hsixth
         y = sp%cof(0) + (x - sp%tmin)*yp
      else if (x > sp%tmax) then
         yp = sp%hi*(sp%cof(sp%n - 1) - sp%cof(sp%n - 2)) &
              + (2.0_wp*sp%dof(sp%n - 1) + sp%dof(sp%n - 2))*sp%hsixth
         y = sp%cof(sp%n - 1) + (x - sp%tmax)*yp
      else
         tt = (x - sp%tmin)*sp%hi
         klo = min(int(tt), sp%n - 2)
         khi = klo + 1
         b = tt - real(klo, wp)
         a = 1.0_wp - b
         a2 = a*a
         b2 = b*b
         y = a*sp%cof(klo) + b*sp%cof(khi) &
             + (a*(a2 - 1.0_wp)*sp%dof(klo) + b*(b2 - 1.0_wp)*sp%dof(khi)) &
             *sp%h2sixth
         yp = sp%hi*(sp%cof(khi) - sp%cof(klo)) &
              + ((3.0_wp*b2 - 1.0_wp)*sp%dof(khi) &
                 - (3.0_wp*a2 - 1.0_wp)*sp%dof(klo))*sp%hsixth
      end if
   end subroutine spline_eval

   !> Three-body radial weight `f(r)` and `df/dr`, held at zero past the
   !! table's last knot so the three-body term has a hard range.
   pure subroutine three_body_radial(par, r, f, fp)
      type(lenosky_params_t), intent(in) :: par
      real(wp), intent(in) :: r
      real(wp), intent(out) :: f, fp

      if (r > par%fff%tmax) then
         f = 0.0_wp
         fp = 0.0_wp
      else
         call spline_eval(par%fff, r, f, fp)
      end if
   end subroutine three_body_radial

   !> Gradient-like factor for the leg atom `self` of one triplet, in the
   !! sign the legacy `f3ij` array carries: the force on `self` is `U'` of
   !! the centre times this vector.
   !!
   !! `w_self` and `w_other` point from each end towards the centre.
   pure function embed_leg_force(f_self, fp_self, rinv_self, w_self, &
                                 f_other, w_other, lcos, g, gp) result(fl)
      real(wp), intent(in) :: f_self, fp_self, rinv_self, w_self(3)
      real(wp), intent(in) :: f_other, w_other(3), lcos, g, gp
      real(wp) :: fl(3)

      real(wp) :: t1, t2

      t1 = fp_self*f_other*g
      t2 = rinv_self*(f_self*f_other)*gp
      fl = w_self*t1 + (w_other - w_self*lcos)*t2
   end function embed_leg_force

   !> Three-body legs of atom `i`: every neighbour the radial weight still
   !! reaches, with that weight, its slope, the unit vector towards `i`, the
   !! inverse distance, and the atom index.
   pure subroutine collect_legs(par, table, i, maxnbr, nleg, f, fp, w, rinv, who)
      type(lenosky_params_t), intent(in) :: par
      type(neighbor_table_t), intent(in) :: table
      integer(ip), intent(in) :: i, maxnbr
      integer(ip), intent(out) :: nleg
      real(wp), intent(out) :: f(maxnbr), fp(maxnbr), w(3, maxnbr), rinv(maxnbr)
      integer(ip), intent(out) :: who(maxnbr)

      integer(ip) :: s
      real(wp) :: r

      nleg = 0_ip
      do s = table%row(i), table%row(i + 1_ip) - 1_ip
         r = table%dist(s)
         if (r > par%fff%tmax) cycle
         nleg = nleg + 1_ip
         call three_body_radial(par, r, f(nleg), fp(nleg))
         w(:, nleg) = -table%vec(:, s)/r
         rinv(nleg) = 1.0_wp/r
         who(nleg) = table%idx(s)
      end do
   end subroutine collect_legs

   !> Energy of atom `i` and the embedding slope `U'(n_i)`.
   !!
   !! Every quantity here depends on atom `i`'s own neighbourhood only, so
   !! the iterations of the calling loop are independent.
   pure subroutine atom_terms(par, table, i, maxnbr, e_i, ep_i)
      type(lenosky_params_t), intent(in) :: par
      type(neighbor_table_t), intent(in) :: table
      integer(ip), intent(in) :: i, maxnbr
      real(wp), intent(out) :: e_i, ep_i

      real(wp) :: f(maxnbr), fp(maxnbr), w(3, maxnbr), rinv(maxnbr)
      integer(ip) :: who(maxnbr)
      integer(ip) :: s, nleg, nj, nk
      real(wp) :: r, dens, e_pair, ep_pair, rho, rhop, lcos, g, gp, e_embed

      e_i = 0.0_wp
      dens = 0.0_wp

      do s = table%row(i), table%row(i + 1_ip) - 1_ip
         r = table%dist(s)
         call spline_eval(par%phi, r, e_pair, ep_pair)
         e_i = e_i + 0.5_wp*e_pair
         call spline_eval(par%rho, r, rho, rhop)
         dens = dens + rho
      end do

      call collect_legs(par, table, i, maxnbr, nleg, f, fp, w, rinv, who)

      do nj = 1_ip, nleg - 1_ip
         do nk = nj + 1_ip, nleg
            if (who(nj) == who(nk)) cycle
            lcos = dot_product(w(:, nj), w(:, nk))
            call spline_eval(par%ggg, lcos, g, gp)
            dens = dens + f(nj)*f(nk)*g
         end do
      end do

      call spline_eval(par%uuu, dens, e_embed, ep_i)
      e_i = e_i + e_embed
   end subroutine atom_terms

   !> Total force on atom `i`, given the embedding slope of every atom.
   pure subroutine atom_force(par, table, i, maxnbr, ep, f_i)
      type(lenosky_params_t), intent(in) :: par
      type(neighbor_table_t), intent(in) :: table
      integer(ip), intent(in) :: i, maxnbr
      real(wp), intent(in) :: ep(:)
      real(wp), intent(out) :: f_i(3)

      real(wp) :: f(maxnbr), fp(maxnbr), w(3, maxnbr), rinv(maxnbr)
      integer(ip) :: who(maxnbr)
      integer(ip) :: s, sn, j, m, n, nleg, nj, nk
      real(wp) :: r, rim, rmn, wij(3), w_self(3), w_other(3)
      real(wp) :: e_pair, ep_pair, rho, rhop, lcos, g, gp
      real(wp) :: f_self, fp_self, f_other, fp_other

      f_i = 0.0_wp

      ! Pair and two-body embedding channels. Both close because each needs
      ! nothing beyond the embedding slope of the two ends, already known.
      do s = table%row(i), table%row(i + 1_ip) - 1_ip
         j = table%idx(s)
         r = table%dist(s)
         wij = -table%vec(:, s)/r

         call spline_eval(par%phi, r, e_pair, ep_pair)
         f_i = f_i - ep_pair*wij

         call spline_eval(par%rho, r, rho, rhop)
         f_i = f_i - (ep(i) + ep(j))*rhop*wij
      end do

      ! Three-body embedding, i at the centre of every pair of its legs.
      call collect_legs(par, table, i, maxnbr, nleg, f, fp, w, rinv, who)

      do nj = 1_ip, nleg - 1_ip
         do nk = nj + 1_ip, nleg
            if (who(nj) == who(nk)) cycle
            lcos = dot_product(w(:, nj), w(:, nk))
            call spline_eval(par%ggg, lcos, g, gp)
            f_i = f_i - ep(i) &
                  *(embed_leg_force(f(nj), fp(nj), rinv(nj), w(:, nj), &
                                    f(nk), w(:, nk), lcos, g, gp) &
                    + embed_leg_force(f(nk), fp(nk), rinv(nk), w(:, nk), &
                                      f(nj), w(:, nj), lcos, g, gp))
         end do
      end do

      ! Three-body embedding, i at an end of every triplet centred on a
      ! neighbour m, weighted by that centre's embedding slope.
      do s = table%row(i), table%row(i + 1_ip) - 1_ip
         m = table%idx(s)
         rim = table%dist(s)
         if (rim > par%fff%tmax) cycle

         ! Leg i points towards centre m, the stored i -> m vector itself.
         w_self = table%vec(:, s)/rim
         call three_body_radial(par, rim, f_self, fp_self)

         do sn = table%row(m), table%row(m + 1_ip) - 1_ip
            n = table%idx(sn)
            if (n == i) cycle
            rmn = table%dist(sn)
            if (rmn > par%fff%tmax) cycle
            w_other = -table%vec(:, sn)/rmn
            call three_body_radial(par, rmn, f_other, fp_other)
            lcos = dot_product(w_self, w_other)
            call spline_eval(par%ggg, lcos, g, gp)
            f_i = f_i + ep(m) &
                  *embed_leg_force(f_self, fp_self, 1.0_wp/rim, w_self, &
                                   f_other, w_other, lcos, g, gp)
         end do
      end do
   end subroutine atom_force

   !> Perpendicular width of the cell along each lattice vector.
   pure function cell_widths(cell) result(w)
      real(wp), intent(in) :: cell(3, 3)
      real(wp) :: w(3)

      real(wp) :: face(3, 3), vol, area
      integer :: k

      face(:, 1) = cross(cell(:, 2), cell(:, 3))
      face(:, 2) = cross(cell(:, 3), cell(:, 1))
      face(:, 3) = cross(cell(:, 1), cell(:, 2))
      vol = abs(dot_product(cell(:, 1), face(:, 1)))

      w = 0.0_wp
      do k = 1, 3
         area = norm2(face(:, k))
         if (area > 0.0_wp) w(k) = vol/area
      end do
   end function cell_widths

   !> Vector cross product.
   pure function cross(p, q) result(r)
      real(wp), intent(in) :: p(3), q(3)
      real(wp) :: r(3)

      r(1) = p(2)*q(3) - p(3)*q(2)
      r(2) = p(3)*q(1) - p(1)*q(3)
      r(3) = p(1)*q(2) - p(2)*q(1)
   end function cross

   !> Energy and forces for `positions` (3 x natoms) in `cell`.
   subroutine lenosky_energy_forces(positions, cell, par, table, energy, &
                                    forces, status, errmsg)
      real(wp), intent(in), contiguous :: positions(:, :)
      real(wp), intent(in) :: cell(3, 3)
      type(lenosky_params_t), intent(in) :: par
      type(neighbor_table_t), intent(inout) :: table
      real(wp), intent(out) :: energy
      real(wp), intent(out), contiguous :: forces(:, :)
      integer, intent(out) :: status
      character(len=:), allocatable, intent(out) :: errmsg

      integer(ip) :: natoms, i, maxnbr
      real(wp) :: rcut, widths(3)
      real(wp), allocatable :: e_atom(:), ep(:)

      natoms = int(size(positions, 2), ip)
      energy = 0.0_wp
      forces = 0.0_wp
      status = 0
      errmsg = ""
      rcut = par%cutoff()

      ! One cell layer has to hold the cutoff, the precondition the legacy
      ! kernel enforced by binning into cells of at least the cutoff width.
      widths = cell_widths(cell)
      if (minval(widths) < rcut) then
         status = 1
         errmsg = "rgpot_lenosky: cell is thinner than the interaction cutoff"
         return
      end if

      call table%build(positions, cell, rcut, status, errmsg)
      if (status /= 0) return

      maxnbr = 1_ip
      do i = 1_ip, natoms
         maxnbr = max(maxnbr, table%row(i + 1_ip) - table%row(i))
      end do

      allocate (e_atom(natoms), source=0.0_wp)
      allocate (ep(natoms), source=0.0_wp)

      ! Pass one: per-atom scalars. Each iteration reads its own
      ! neighbourhood and writes its own slots.
      do concurrent(i=1:natoms)
         call atom_terms(par, table, i, maxnbr, e_atom(i), ep(i))
      end do

      ! Pass two: forces. Each iteration reads the pass-one array and
      ! writes only its own force column.
      do concurrent(i=1:natoms)
         call atom_force(par, table, i, maxnbr, ep, forces(:, i))
      end do

      energy = sum(e_atom)
   end subroutine lenosky_energy_forces

end module rgpot_lenosky
