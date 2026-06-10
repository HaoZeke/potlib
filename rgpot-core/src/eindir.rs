//! Adapter: rgpot potentials become eindir objectives, consumed THROUGH the
//! eindir C API. rgpot-core links libeindir_core (the cargo capi output, see
//! build.rs) and declares the C entry points with extern "C"; it does NOT
//! depend on the eindir-core Rust crate. This is the metatensor shape: a
//! consumer links the C library through the header, not the core crate.

use std::os::raw::c_void;

use dlpk::sys::DLManagedTensorVersioned;

use crate::potential::PotentialImpl;
use crate::status::rgpot_status_t;
use crate::tensor::{
    rgpot_tensor_cpu_f64_2d, rgpot_tensor_cpu_f64_matrix3, rgpot_tensor_cpu_i32_1d,
    rgpot_tensor_free,
};
use crate::types::{rgpot_force_input_t, rgpot_force_out_t};

// --- eindir C API: declared locally, resolved by linking libeindir_core -----
#[repr(C)]
pub struct eindir_objective_t {
    _private: [u8; 0],
}

#[repr(C)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum eindir_status_t {
    EINDIR_SUCCESS = 0,
    EINDIR_INVALID_PARAMETER = 1,
    EINDIR_INTERNAL_ERROR = 2,
}

pub type EindirEvalFn =
    unsafe extern "C" fn(*mut c_void, *const DLManagedTensorVersioned, *mut f64) -> eindir_status_t;
pub type EindirGradFn = unsafe extern "C" fn(
    *mut c_void,
    *const DLManagedTensorVersioned,
    *mut DLManagedTensorVersioned,
) -> eindir_status_t;
pub type EindirFreeFn = unsafe extern "C" fn(*mut c_void);

extern "C" {
    fn eindir_objective_new(
        dim: usize,
        bounds_low: *const DLManagedTensorVersioned,
        bounds_high: *const DLManagedTensorVersioned,
        eval_fn: EindirEvalFn,
        grad_fn: Option<EindirGradFn>,
        user_data: *mut c_void,
        free_fn: Option<EindirFreeFn>,
    ) -> *mut eindir_objective_t;
    fn eindir_objective_free(obj: *mut eindir_objective_t);
    fn eindir_objective_eval(
        obj: *const eindir_objective_t,
        x: *const DLManagedTensorVersioned,
        value_out: *mut f64,
    ) -> eindir_status_t;
    fn eindir_objective_has_grad(obj: *const eindir_objective_t) -> i32;
}

// --- rgpot -> eindir adapter (callbacks compute energy and -forces) ---------
struct RgpotObjectiveCtx {
    pot: PotentialImpl,
    n_atoms: usize,
    atomic_numbers: Vec<i32>,
    box_matrix: [f64; 9],
}

unsafe extern "C" fn rgpot_eval_cb(
    user_data: *mut c_void,
    x: *const DLManagedTensorVersioned,
    value_out: *mut f64,
) -> eindir_status_t {
    let ctx = unsafe { &*(user_data as *const RgpotObjectiveCtx) };
    let xt = unsafe { &(*x).dl_tensor };
    let n = ctx.n_atoms * 3;
    let x_data = unsafe { std::slice::from_raw_parts(xt.data as *const f64, n) };
    let mut pos = x_data.to_vec();
    let mut atmnrs = ctx.atomic_numbers.clone();
    let mut box_ = ctx.box_matrix;
    let input = rgpot_force_input_t {
        positions: unsafe { rgpot_tensor_cpu_f64_2d(pos.as_mut_ptr(), ctx.n_atoms as i64, 3) },
        atomic_numbers: unsafe { rgpot_tensor_cpu_i32_1d(atmnrs.as_mut_ptr(), ctx.n_atoms as i64) },
        box_matrix: unsafe { rgpot_tensor_cpu_f64_matrix3(box_.as_mut_ptr()) },
    };
    let mut output = rgpot_force_out_t { forces: std::ptr::null_mut(), energy: 0.0, variance: 0.0 };
    let status = unsafe { ctx.pot.calculate(&input, &mut output) };
    unsafe {
        rgpot_tensor_free(output.forces);
        rgpot_tensor_free(input.positions);
        rgpot_tensor_free(input.atomic_numbers);
        rgpot_tensor_free(input.box_matrix);
    }
    if status != rgpot_status_t::RGPOT_SUCCESS {
        return eindir_status_t::EINDIR_INTERNAL_ERROR;
    }
    unsafe { *value_out = output.energy };
    eindir_status_t::EINDIR_SUCCESS
}

unsafe extern "C" fn rgpot_grad_cb(
    user_data: *mut c_void,
    x: *const DLManagedTensorVersioned,
    grad_out: *mut DLManagedTensorVersioned,
) -> eindir_status_t {
    let ctx = unsafe { &*(user_data as *const RgpotObjectiveCtx) };
    let xt = unsafe { &(*x).dl_tensor };
    let n = ctx.n_atoms * 3;
    let x_data = unsafe { std::slice::from_raw_parts(xt.data as *const f64, n) };
    let mut pos = x_data.to_vec();
    let mut atmnrs = ctx.atomic_numbers.clone();
    let mut box_ = ctx.box_matrix;
    let input = rgpot_force_input_t {
        positions: unsafe { rgpot_tensor_cpu_f64_2d(pos.as_mut_ptr(), ctx.n_atoms as i64, 3) },
        atomic_numbers: unsafe { rgpot_tensor_cpu_i32_1d(atmnrs.as_mut_ptr(), ctx.n_atoms as i64) },
        box_matrix: unsafe { rgpot_tensor_cpu_f64_matrix3(box_.as_mut_ptr()) },
    };
    let mut output = rgpot_force_out_t { forces: std::ptr::null_mut(), energy: 0.0, variance: 0.0 };
    let status = unsafe { ctx.pot.calculate(&input, &mut output) };
    if status == rgpot_status_t::RGPOT_SUCCESS && !output.forces.is_null() {
        let ft = unsafe { &(*output.forces).dl_tensor };
        let src = unsafe { std::slice::from_raw_parts(ft.data as *const f64, n) };
        let gt = unsafe { &(*grad_out).dl_tensor };
        let dst = unsafe { std::slice::from_raw_parts_mut(gt.data as *mut f64, n) };
        for i in 0..n {
            dst[i] = -src[i]; // gradient = -force
        }
    }
    unsafe {
        rgpot_tensor_free(output.forces);
        rgpot_tensor_free(input.positions);
        rgpot_tensor_free(input.atomic_numbers);
        rgpot_tensor_free(input.box_matrix);
    }
    if status != rgpot_status_t::RGPOT_SUCCESS {
        return eindir_status_t::EINDIR_INTERNAL_ERROR;
    }
    eindir_status_t::EINDIR_SUCCESS
}

unsafe extern "C" fn rgpot_ctx_free(ptr: *mut c_void) {
    if !ptr.is_null() {
        drop(unsafe { Box::from_raw(ptr as *mut RgpotObjectiveCtx) });
    }
}

/// Wrap an rgpot potential as an eindir objective, built through the eindir C
/// API (`eindir_objective_new`, resolved by linking libeindir_core). Energy is
/// the objective value; the negative forces are the gradient.
#[no_mangle]
pub unsafe extern "C" fn eindir_objective_from_rgpot(
    pot: *mut crate::potential::rgpot_potential_t,
    n_atoms: usize,
    atomic_numbers: *const i32,
    box_matrix: *const f64,
    bounds_low: *const DLManagedTensorVersioned,
    bounds_high: *const DLManagedTensorVersioned,
) -> *mut eindir_objective_t {
    if pot.is_null() || atomic_numbers.is_null() || box_matrix.is_null() {
        return std::ptr::null_mut();
    }
    let pot_owned = unsafe { *Box::from_raw(pot) };
    let atmnrs = unsafe { std::slice::from_raw_parts(atomic_numbers, n_atoms) }.to_vec();
    let mut box_arr = [0.0f64; 9];
    unsafe { std::ptr::copy_nonoverlapping(box_matrix, box_arr.as_mut_ptr(), 9) };
    let ctx = Box::new(RgpotObjectiveCtx { pot: pot_owned, n_atoms, atomic_numbers: atmnrs, box_matrix: box_arr });
    let ctx_ptr = Box::into_raw(ctx) as *mut c_void;
    let dim = n_atoms * 3;
    unsafe {
        eindir_objective_new(
            dim,
            bounds_low,
            bounds_high,
            rgpot_eval_cb,
            Some(rgpot_grad_cb),
            ctx_ptr,
            Some(rgpot_ctx_free),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tensor::create_owned_f64_tensor;
    use dlpk::sys::{DLDataType, DLDataTypeCode, DLDevice, DLDeviceType, DLPackVersion, DLTensor};

    // Mock rgpot potential: energy = sum of positions, forces = -1 everywhere.
    unsafe extern "C" fn mock_energy_callback(
        _user_data: *mut c_void,
        input: *const rgpot_force_input_t,
        output: *mut rgpot_force_out_t,
    ) -> rgpot_status_t {
        let inp = unsafe { &*input };
        let out = unsafe { &mut *output };
        let n = unsafe { inp.n_atoms() }.unwrap_or(0);
        let pt = unsafe { &(*inp.positions).dl_tensor };
        let pos_data = unsafe { std::slice::from_raw_parts(pt.data as *const f64, n * 3) };
        out.energy = pos_data.iter().sum();
        out.variance = 0.0;
        out.forces = create_owned_f64_tensor(vec![-1.0f64; n * 3], vec![n as i64, 3]);
        rgpot_status_t::RGPOT_SUCCESS
    }

    struct Ctx1d {
        shape: [i64; 1],
        strides: [i64; 1],
    }
    unsafe extern "C" fn del1d(ptr: *mut DLManagedTensorVersioned) {
        if ptr.is_null() {
            return;
        }
        let ctx = unsafe { (*ptr).manager_ctx.cast::<Ctx1d>() };
        if !ctx.is_null() {
            drop(unsafe { Box::from_raw(ctx) });
        }
        drop(unsafe { Box::from_raw(ptr) });
    }
    fn make_1d(data: *mut f64, n: usize) -> *mut DLManagedTensorVersioned {
        let mut ctx = Box::new(Ctx1d { shape: [n as i64], strides: [1] });
        let t = DLTensor {
            data: data.cast(),
            device: DLDevice { device_type: DLDeviceType::kDLCPU, device_id: 0 },
            ndim: 1,
            dtype: DLDataType { code: DLDataTypeCode::kDLFloat, bits: 64, lanes: 1 },
            shape: ctx.shape.as_mut_ptr(),
            strides: ctx.strides.as_mut_ptr(),
            byte_offset: 0,
        };
        let managed = Box::new(DLManagedTensorVersioned {
            version: DLPackVersion { major: 1, minor: 0 },
            manager_ctx: Box::into_raw(ctx).cast(),
            deleter: Some(del1d),
            flags: 0,
            dl_tensor: t,
        });
        Box::into_raw(managed)
    }

    // The hourglass: a rgpot potential becomes an eindir objective and is
    // evaluated entirely through the eindir C API, which is resolved by LINKING
    // libeindir_core. No eindir-core Rust crate is involved.
    #[test]
    fn rgpot_through_eindir_c_api() {
        let pot = PotentialImpl::new(mock_energy_callback, std::ptr::null_mut(), None);
        let pot_ptr = Box::into_raw(Box::new(pot));
        let n_atoms = 2usize;
        let dim = n_atoms * 3;
        let atmnrs = [1i32, 1];
        let mut low_data = vec![-50.0f64; dim];
        let mut high_data = vec![50.0f64; dim];
        let low_t = make_1d(low_data.as_mut_ptr(), dim);
        let high_t = make_1d(high_data.as_mut_ptr(), dim);
        let obj = unsafe {
            eindir_objective_from_rgpot(
                pot_ptr,
                n_atoms,
                atmnrs.as_ptr(),
                [10.0, 0.0, 0.0, 0.0, 10.0, 0.0, 0.0, 0.0, 10.0].as_ptr(),
                low_t,
                high_t,
            )
        };
        assert!(!obj.is_null());
        let mut x_data = [1.0, 2.0, 3.0, 4.0, 5.0, 6.0f64];
        let x_t = make_1d(x_data.as_mut_ptr(), dim);
        let mut value = 0.0;
        let s = unsafe { eindir_objective_eval(obj, x_t, &mut value) };
        assert_eq!(s, eindir_status_t::EINDIR_SUCCESS);
        assert_eq!(value, 21.0); // 1+2+3+4+5+6
        assert_eq!(unsafe { eindir_objective_has_grad(obj) }, 1);
        unsafe {
            rgpot_tensor_free(low_t);
            rgpot_tensor_free(high_t);
            rgpot_tensor_free(x_t);
            eindir_objective_free(obj);
        }
    }
}
