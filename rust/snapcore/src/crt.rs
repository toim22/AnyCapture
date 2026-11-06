//! printf of the C runtime, for text that has to come out as the C++ wrote it (FormatA, snprintf): the same static CRT
//! function with the options the C++ passed. The C++ startup code turns on standard rounding (an exact tie goes to the
//! even digit: 0.125 → "0.12"), so that is asked for here too.

use std::ffi::{c_char, c_void};

unsafe extern "C" {
    fn __stdio_common_vsprintf(
        options: u64,
        buffer: *mut c_char,
        count: usize,
        format: *const c_char,
        locale: *mut c_void,
        args: *const u64,
    ) -> i32;
}

/// _CRT_INTERNAL_PRINTF_STANDARD_SNPRINTF_BEHAVIOR: the length is returned when the buffer is too small (snprintf).
const STANDARD_SNPRINTF: u64 = 2;
/// _CRT_INTERNAL_PRINTF_STANDARD_ROUNDING, which the C++ startup sets for every C++ printf.
const STANDARD_ROUNDING: u64 = 32;

/// One printf argument; on x64 each takes an 8-byte slot of the va_list.
#[derive(Clone, Copy, Debug)]
pub enum Arg {
    I(i64),
    F(f64),
}

/// sprintf with numeric arguments (%d, %zu, %f, %g…).
pub fn sprintf(format: &str, args: &[Arg]) -> String {
    let fmt: Vec<u8> = format.bytes().chain(std::iter::once(0)).collect();
    let slots: Vec<u64> = args
        .iter()
        .map(|a| match *a {
            Arg::I(v) => v as u64,
            Arg::F(v) => v.to_bits(),
        })
        .collect();
    let call = |buf: *mut c_char, len: usize| unsafe {
        __stdio_common_vsprintf(STANDARD_SNPRINTF | STANDARD_ROUNDING, buf, len, fmt.as_ptr() as _, std::ptr::null_mut(), slots.as_ptr())
    };
    let n = call(std::ptr::null_mut(), 0);
    if n <= 0 {
        return String::new();
    }
    let mut buf = vec![0u8; n as usize + 1];
    let n = call(buf.as_mut_ptr() as _, buf.len()).max(0) as usize;
    buf.truncate(n);
    String::from_utf8_lossy(&buf).into_owned()
}

/// "%.{decimals}f" of one value.
pub fn fixed(v: f64, decimals: u32) -> String {
    sprintf(&format!("%.{decimals}f"), &[Arg::F(v)])
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn formats_like_the_crt() {
        assert_eq!(fixed(0.125, 2), "0.12");
        assert_eq!(fixed(0.375, 2), "0.38");
        assert_eq!(fixed(0.03125, 4), "0.0312");
        assert_eq!(fixed(1.0625, 3), "1.062");
        assert_eq!(fixed(-0.0, 2), "-0.00");
        assert_eq!(fixed(595.28, 2), "595.28");
        assert_eq!(sprintf("%zu 0 obj %d %g", &[Arg::I(12), Arg::I(-3), Arg::F(0.5)]), "12 0 obj -3 0.5");
        assert_eq!(sprintf("%010zu 00000 n ", &[Arg::I(1234)]), "0000001234 00000 n ");
    }
}
