! MIT License
! Copyright 2023--present rgpot developers

!> Error channel shared by the Fortran potentials' C entry points.
!!
!! Legacy kernels reported failures by writing to unit 6 and calling
!! `stop`, which takes the host process down. The ported kernels return a
!! status instead and leave the message here for the C++ wrapper to turn
!! into an exception. One buffer suffices: the Fortran potentials declare
!! `Reentrancy::ProcessSerial`, so evaluations never overlap.
module rgpot_ferror
   use, intrinsic :: iso_c_binding, only: c_char, c_int, c_null_char
   implicit none
   private

   public :: set_error, clear_error, rgpot_fortran_last_error

   integer, parameter :: max_len = 512
   character(len=max_len) :: message = ""
   integer :: message_len = 0

contains

   !> Record a failure message for the next `rgpot_fortran_last_error`.
   subroutine set_error(text)
      character(len=*), intent(in) :: text

      message_len = min(len_trim(text), max_len)
      message = text(1:message_len)
   end subroutine set_error

   !> Forget any recorded message.
   subroutine clear_error()
      message = ""
      message_len = 0
   end subroutine clear_error

   !> Copy the last message into `buffer` as a NUL-terminated C string.
   !!
   !! Returns the number of characters written, excluding the terminator.
   function rgpot_fortran_last_error(buffer, buffer_len) result(written) &
      bind(c, name="rgpot_fortran_last_error")
      character(kind=c_char), intent(out) :: buffer(*)
      integer(c_int), value, intent(in) :: buffer_len
      integer(c_int) :: written

      integer :: k, room

      room = int(buffer_len) - 1
      written = int(min(message_len, max(room, 0)), c_int)

      do k = 1, int(written)
         buffer(k) = message(k:k)
      end do
      if (buffer_len > 0_c_int) buffer(int(written) + 1) = c_null_char
   end function rgpot_fortran_last_error

end module rgpot_ferror
