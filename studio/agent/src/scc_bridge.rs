//! Safe-ish wrapper around the libscc C ABI.
//!
//! Every `unsafe { ... }` is paired with a `// SAFETY:` comment
//! immediately above explaining the invariant that justifies the
//! call. [Ultrathink #1]
//!
//! Threading: an `Encoder` / `Decoder` owns one `scc_ctx*`. The C ABI
//! contract (per ADR-006) is "one ctx per thread"; these structs are
//! intentionally NOT `Send`/`Sync` (the raw pointer is left bare so
//! the compiler enforces that).

use std::ffi::{CStr, CString};
use std::os::raw::c_char;
use std::ptr;

use thiserror::Error;

use crate::scc_sys as sys;

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Profile {
    Lossless,
    LossyHigh,
    LossyStreaming,
}

impl Profile {
    fn params(self) -> [(&'static str, &'static str); 4] {
        match self {
            Profile::Lossless => [
                ("mode_flags", "1"),
                ("top_count",  "4"),
                ("tau_static", "1"),
                ("tau_low",    "5"),
            ],
            Profile::LossyHigh => [
                ("mode_flags", "0"),
                ("top_count",  "4"),
                ("tau_static", "5"),
                ("tau_low",    "30"),
            ],
            Profile::LossyStreaming => [
                ("mode_flags", "2"),
                ("top_count",  "4"),
                ("tau_static", "10"),
                ("tau_low",    "50"),
            ],
        }
    }
}

#[derive(Debug, Error)]
pub enum BridgeError {
    #[error("scc_init failed")]
    Init,
    #[error("scc_set_param {key}={value} failed: {message}")]
    SetParam { key: String, value: String, message: String },
    #[error("scc_encode_frame probe failed: {0}")]
    EncodeProbe(String),
    #[error("scc_encode_frame failed (rc={rc}): {message}")]
    Encode { rc: u32, message: String },
    #[error("scc_decode_sei probe failed: {0}")]
    DecodeProbe(String),
    #[error("scc_decode_sei failed (rc={rc}): {message}")]
    Decode { rc: u32, message: String },
    #[error("input length {len} does not equal width*height ({expected})")]
    InputLength { len: usize, expected: usize },
    #[error("CString conversion: interior NUL")]
    InteriorNul,
}

pub type Result<T> = std::result::Result<T, BridgeError>;

// ---------------------------------------------------------------------------
// Encoder
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct Encoder {
    ctx: *mut sys::scc_ctx,
    width: i32,
    height: i32,
    bit_depth: i32,
}

impl Encoder {
    pub fn new(width: i32, height: i32, bit_depth: i32, profile: Profile) -> Result<Self> {
        if !(bit_depth == 8 || bit_depth == 12 || bit_depth == 16) {
            return Err(BridgeError::SetParam {
                key: "bit_depth".into(),
                value: bit_depth.to_string(),
                message: "bit_depth must be 8, 12, or 16".into(),
            });
        }
        // SAFETY: scc_init takes no args; the C ABI returns NULL on failure
        // which we check immediately.
        let ctx = unsafe { sys::scc_init() };
        if ctx.is_null() {
            return Err(BridgeError::Init);
        }
        let mut enc = Encoder { ctx, width, height, bit_depth };
        for (key, value) in profile.params() {
            enc.set_param(key, value)?;
        }
        Ok(enc)
    }

    fn set_param(&mut self, key: &str, value: &str) -> Result<()> {
        let key_c = CString::new(key).map_err(|_| BridgeError::InteriorNul)?;
        let val_c = CString::new(value).map_err(|_| BridgeError::InteriorNul)?;
        // SAFETY: ctx is non-null (constructor validates); both CString
        // handles are NUL-terminated and live for the duration of the call.
        let rc = unsafe { sys::scc_set_param(self.ctx, key_c.as_ptr(), val_c.as_ptr()) };
        if rc != sys::scc_result_SCC_OK {
            return Err(BridgeError::SetParam {
                key: key.into(),
                value: value.into(),
                message: self.last_error(),
            });
        }
        Ok(())
    }

    pub fn encode(&mut self, depth: &[u16]) -> Result<Vec<u8>> {
        let expected = (self.width as usize) * (self.height as usize);
        if depth.len() != expected {
            return Err(BridgeError::InputLength {
                len: depth.len(),
                expected,
            });
        }
        let depth_ptr = depth.as_ptr() as *const u8;
        let stride: usize = (self.width as usize) * std::mem::size_of::<u16>();

        // Probe required output size.
        let mut required: usize = 0;
        // SAFETY: ctx non-null. depth slice is valid for `expected * 2`
        // bytes (Rust guarantee). out_sei is NULL with out_cap = 0, which
        // signals probe mode to the C ABI; the only side effect is *out_len
        // being written.
        unsafe {
            sys::scc_encode_frame(
                self.ctx,
                depth_ptr,
                stride,
                self.width,
                self.height,
                self.bit_depth,
                ptr::null_mut(),
                0,
                &mut required as *mut usize,
            );
        }
        if required == 0 {
            return Err(BridgeError::EncodeProbe(self.last_error()));
        }

        let mut buf = vec![0u8; required];
        let mut written: usize = 0;
        // SAFETY: ctx non-null. depth slice as above. buf.as_mut_ptr() is
        // valid for `required` bytes (just allocated). After the call we
        // truncate to *written to drop trailing zero bytes from the spare
        // capacity.
        let rc = unsafe {
            sys::scc_encode_frame(
                self.ctx,
                depth_ptr,
                stride,
                self.width,
                self.height,
                self.bit_depth,
                buf.as_mut_ptr(),
                required,
                &mut written as *mut usize,
            )
        };
        if rc != sys::scc_result_SCC_OK {
            return Err(BridgeError::Encode { rc, message: self.last_error() });
        }
        buf.truncate(written);
        Ok(buf)
    }

    fn last_error(&self) -> String {
        // SAFETY: ctx non-null. scc_get_last_error returns either NULL
        // (no error recorded) or a NUL-terminated C string owned by the
        // C ABI's thread-local storage; valid until the next libscc call
        // on this thread.
        let p = unsafe { sys::scc_get_last_error(self.ctx) };
        if p.is_null() {
            return String::new();
        }
        // SAFETY: p is a NUL-terminated C string per the ABI contract.
        unsafe { cstr_to_string(p) }
    }
}

impl Drop for Encoder {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            // SAFETY: ctx non-null. scc_destroy invalidates the pointer;
            // we null it out so a stray double-drop is a no-op.
            unsafe { sys::scc_destroy(self.ctx) };
            self.ctx = ptr::null_mut();
        }
    }
}

// Encoder owns a *mut sys::scc_ctx. Raw pointers are !Send + !Sync by
// default, so the compiler already rejects cross-thread misuse without
// needing explicit negative impls (which would require nightly).

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

#[derive(Debug)]
pub struct Decoder {
    ctx: *mut sys::scc_ctx,
}

#[derive(Clone, Debug)]
pub struct DecodedFrame {
    pub depth: Vec<u16>,
    pub width: i32,
    pub height: i32,
    pub bit_depth: i32,
}

impl Decoder {
    pub fn new() -> Result<Self> {
        // SAFETY: as in Encoder::new.
        let ctx = unsafe { sys::scc_init() };
        if ctx.is_null() {
            return Err(BridgeError::Init);
        }
        Ok(Decoder { ctx })
    }

    pub fn decode(&mut self, sei: &[u8]) -> Result<DecodedFrame> {
        let mut w: i32 = 0;
        let mut h: i32 = 0;
        let mut bd: i32 = 0;
        // SAFETY: ctx non-null. sei.as_ptr()/len describe a valid byte slice.
        // out_depth=NULL signals probe mode; the call only writes the
        // dimension out-pointers.
        unsafe {
            sys::scc_decode_sei(
                self.ctx,
                sei.as_ptr(),
                sei.len(),
                ptr::null_mut(),
                0,
                &mut w as *mut i32,
                &mut h as *mut i32,
                &mut bd as *mut i32,
            );
        }
        if w <= 0 || h <= 0 {
            return Err(BridgeError::DecodeProbe(self.last_error()));
        }

        let mut depth = vec![0u16; (w as usize) * (h as usize)];
        let stride = (w as usize) * std::mem::size_of::<u16>();
        // SAFETY: ctx non-null. sei slice valid. depth.as_mut_ptr() valid
        // for w*h*2 bytes. The C ABI writes exactly that many bytes on
        // success.
        let rc = unsafe {
            sys::scc_decode_sei(
                self.ctx,
                sei.as_ptr(),
                sei.len(),
                depth.as_mut_ptr() as *mut u8,
                stride,
                &mut w as *mut i32,
                &mut h as *mut i32,
                &mut bd as *mut i32,
            )
        };
        if rc != sys::scc_result_SCC_OK {
            return Err(BridgeError::Decode { rc, message: self.last_error() });
        }
        Ok(DecodedFrame { depth, width: w, height: h, bit_depth: bd })
    }

    fn last_error(&self) -> String {
        // SAFETY: see Encoder::last_error.
        let p = unsafe { sys::scc_get_last_error(self.ctx) };
        if p.is_null() {
            return String::new();
        }
        // SAFETY: p is NUL-terminated per ABI contract.
        unsafe { cstr_to_string(p) }
    }
}

impl Drop for Decoder {
    fn drop(&mut self) {
        if !self.ctx.is_null() {
            // SAFETY: ctx non-null; freed once.
            unsafe { sys::scc_destroy(self.ctx) };
            self.ctx = ptr::null_mut();
        }
    }
}

// Decoder is also !Send + !Sync via the raw-pointer field; see Encoder.

// ---------------------------------------------------------------------------
// Helper: read a C string from a raw pointer. Marked unsafe with an explicit
// SAFETY contract so callers must justify the call.
// ---------------------------------------------------------------------------

/// # Safety
/// `p` MUST be either null or a pointer to a NUL-terminated byte sequence.
unsafe fn cstr_to_string(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    // SAFETY: caller's contract.
    let cstr = unsafe { CStr::from_ptr(p) };
    cstr.to_string_lossy().into_owned()
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn make_depth(w: i32, h: i32) -> Vec<u16> {
        let mut s: u32 = 0xC0FFEE;
        let n = (w * h) as usize;
        let mut out = vec![0u16; n];
        for v in &mut out {
            s = s.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            *v = (s & 0x3FF) as u16;
        }
        out
    }

    // The encode/decode tests are gated on the libscc dynamic library
    // being present at link time. If we link successfully these run; if
    // the host has no libscc.so they're skipped via the link error.
    #[test]
    fn roundtrip_small_frame() {
        let mut enc = Encoder::new(16, 16, 12, Profile::Lossless).expect("Encoder::new");
        let depth = make_depth(16, 16);
        let sei = enc.encode(&depth).expect("encode");
        assert!(!sei.is_empty());
        let mut dec = Decoder::new().expect("Decoder::new");
        let out = dec.decode(&sei).expect("decode");
        assert_eq!(out.width, 16);
        assert_eq!(out.height, 16);
        assert_eq!(out.bit_depth, 12);
        assert_eq!(out.depth, depth);
    }
}
