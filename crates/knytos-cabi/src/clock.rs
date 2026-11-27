//! Caller-owned UTC samples and bounded local extrapolation. These scalars
//! carry no authority and never assert that projected time remains synchronized.
#[cfg(any(target_os = "none", test))]
use crate::{
    DEADLINE,
    INVALID,
    OVERFLOW,
    UNAVAILABLE,
    counter_nanoseconds,
};

#[repr(C)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct RealtimeSample {
    pub utc_ns: u64,
    pub monotonic_ticks: u64,
    pub counter_frequency_hz: u64,
    pub drift_ppb: i64,
    pub last_sync_age_ms: u64,
    pub uncertainty_ms: u32,
    pub reserved: u32,
}

#[cfg(any(target_os = "none", test))]
impl RealtimeSample {
    pub(crate) fn validate(&self) -> Result<(), i32> {
        if self.counter_frequency_hz == 0
            || self.drift_ppb <= -1_000_000_000
            || self.last_sync_age_ms == u64::MAX
            || self.reserved != 0
        {
            Err(INVALID)
        } else {
            Ok(())
        }
    }

    /// One immutable sample and one counter read. No lock, refresh, RPC or
    /// retry. The limit measures nominal elapsed time since the sample, not
    /// time since the upstream NTP synchronization or a UTC accuracy bound.
    pub(crate) fn project(
        &self,
        ticks: u64,
        frequency: u64,
        max_elapsed_ns: u64,
    ) -> Result<u64, i32> {
        self.validate()?;
        if frequency != self.counter_frequency_hz || ticks < self.monotonic_ticks {
            return Err(UNAVAILABLE);
        }
        let elapsed = counter_nanoseconds(ticks - self.monotonic_ticks, frequency)?;
        if elapsed > max_elapsed_ns {
            return Err(DEADLINE);
        }
        // Match the producer's two flooring steps: ticks -> nominal ns,
        // then positive rate scaling. Re-anchoring a floored service sample
        // can differ from its original model by a few nanoseconds; a later
        // service correction/clamp can differ by more. This is an estimate.
        let scale = (1_000_000_000i128 + i128::from(self.drift_ppb)) as u128;
        let corrected = u128::from(elapsed).checked_mul(scale).ok_or(OVERFLOW)? / 1_000_000_000;
        let corrected = u64::try_from(corrected).map_err(|_| OVERFLOW)?;
        self.utc_ns.checked_add(corrected).ok_or(OVERFLOW)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample() -> RealtimeSample {
        RealtimeSample {
            utc_ns: 1_700_000_000_000_000_000,
            monotonic_ticks: 100,
            counter_frequency_hz: 10,
            drift_ppb: 0,
            last_sync_age_ms: 50,
            uncertainty_ms: 20,
            reserved: 0,
        }
    }

    #[test]
    fn c_layout_is_six_aligned_words_with_explicit_reserved_bytes() {
        assert_eq!(core::mem::size_of::<RealtimeSample>(), 48);
        assert_eq!(core::mem::align_of::<RealtimeSample>(), 8);
        assert_eq!(core::mem::offset_of!(RealtimeSample, drift_ppb), 24);
        assert_eq!(core::mem::offset_of!(RealtimeSample, last_sync_age_ms), 32);
        assert_eq!(core::mem::offset_of!(RealtimeSample, uncertainty_ms), 40);
        assert_eq!(core::mem::offset_of!(RealtimeSample, reserved), 44);
    }

    #[test]
    fn projection_uses_rate_and_accepts_the_exact_age_boundary() {
        let mut value = sample();
        assert_eq!(value.project(100, 10, 0), Ok(value.utc_ns));
        for (drift, elapsed) in
            [(0, 1_000_000_000), (500_000, 1_000_500_000), (-500_000, 999_500_000)]
        {
            value.drift_ppb = drift;
            assert_eq!(value.project(110, 10, 1_000_000_000), Ok(value.utc_ns + elapsed));
        }
        assert_eq!(value.project(110, 10, 999_999_999), Err(DEADLINE));
        value.counter_frequency_hz = 3;
        value.drift_ppb = 500_000;
        assert_eq!(value.project(101, 3, 333_333_333), Ok(value.utc_ns + 333_499_999));
    }

    #[test]
    fn projection_rejects_invalid_metadata_and_changed_or_reversed_counters() {
        for field in 0..4 {
            let mut value = sample();
            match field {
                0 => value.reserved = 1,
                1 => value.counter_frequency_hz = 0,
                2 => value.drift_ppb = -1_000_000_000,
                _ => value.last_sync_age_ms = u64::MAX,
            }
            assert_eq!(value.project(100, 10, u64::MAX), Err(INVALID));
        }
        let mut value = sample();
        value.drift_ppb = i64::MIN;
        assert_eq!(value.project(100, 10, u64::MAX), Err(INVALID));
        assert_eq!(sample().project(99, 10, u64::MAX), Err(UNAVAILABLE));
        assert_eq!(sample().project(110, 11, u64::MAX), Err(UNAVAILABLE));
        assert_eq!(sample().project(110, 0, u64::MAX), Err(UNAVAILABLE));
    }

    #[test]
    fn projection_checks_nominal_scaled_and_utc_overflow_without_saturating() {
        let mut value = sample();
        value.utc_ns = 0;
        value.monotonic_ticks = 0;
        value.counter_frequency_hz = 1_000_000_000;
        assert_eq!(value.project(u64::MAX, 1_000_000_000, u64::MAX), Ok(u64::MAX));
        value.utc_ns = 1;
        assert_eq!(value.project(u64::MAX, 1_000_000_000, u64::MAX), Err(OVERFLOW));
        value.utc_ns = 0;
        value.drift_ppb = 1;
        assert_eq!(value.project(u64::MAX, 1_000_000_000, u64::MAX), Err(OVERFLOW));
        value.drift_ppb = 0;
        value.counter_frequency_hz = 1;
        assert_eq!(value.project(u64::MAX, 1, u64::MAX), Err(OVERFLOW));
        value.counter_frequency_hz = 1_000_000_000;
        value.drift_ppb = i64::MAX;
        assert_eq!(value.project(u64::MAX, 1_000_000_000, u64::MAX), Err(OVERFLOW));
    }
}
