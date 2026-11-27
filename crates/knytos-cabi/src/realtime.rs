//! Checked UTC payload conversion, independent of the native IPC backend.
//! Calendar time is not authenticated security time merely because its source
//! has reached the time service's synchronized state.
use knytos_protocol_time as time;

use crate::{
    OVERFLOW,
    RealtimeSample,
    UNAVAILABLE,
};

pub(crate) const INVALID_REPLY: i32 = 9;

#[cfg(test)]
fn decode(bytes: &[u8]) -> Result<u64, i32> {
    decode_sample(bytes).map(|sample| sample.utc_ns)
}

pub(crate) fn decode_sample(bytes: &[u8]) -> Result<RealtimeSample, i32> {
    if bytes.len() != time::SNAPSHOT_LEN
        || bytes.get(66..68) != Some(&[0, 0][..])
        || bytes.get(76..80) != Some(&[0, 0, 0, 0][..])
    {
        return Err(INVALID_REPLY);
    }
    let snapshot = time::TimeSnapshot::decode(bytes).ok_or(INVALID_REPLY)?;
    match snapshot.state {
        time::STATE_UNSYNCHRONIZED | time::STATE_HOLDOVER => return Err(UNAVAILABLE),
        time::STATE_SYNCHRONIZED => {}
        _ => return Err(INVALID_REPLY),
    }
    if snapshot.nanosecond >= 1_000_000_000
        || snapshot.counter_frequency_hz == 0
        || snapshot.utc != time::utc_from_unix(snapshot.unix_seconds)
    {
        return Err(INVALID_REPLY);
    }
    let seconds = u64::try_from(snapshot.unix_seconds).map_err(|_| OVERFLOW)?;
    let utc_ns = seconds
        .checked_mul(1_000_000_000)
        .and_then(|ns| ns.checked_add(u64::from(snapshot.nanosecond)))
        .ok_or(OVERFLOW)?;
    let sample = RealtimeSample {
        utc_ns,
        monotonic_ticks: snapshot.monotonic_ticks,
        counter_frequency_hz: snapshot.counter_frequency_hz,
        drift_ppb: snapshot.drift_ppb,
        last_sync_age_ms: snapshot.last_sync_age_ms,
        uncertainty_ms: snapshot.uncertainty_ms,
        reserved: 0,
    };
    sample.validate().map_err(|_| INVALID_REPLY)?;
    Ok(sample)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn snapshot(seconds: i64, nanos: u32) -> time::TimeSnapshot {
        time::TimeSnapshot {
            state: time::STATE_SYNCHRONIZED,
            stratum: 3,
            unix_seconds: seconds,
            nanosecond: nanos,
            uncertainty_ms: 20,
            drift_ppb: -100,
            last_sync_age_ms: 42,
            monotonic_ticks: 1000,
            counter_frequency_hz: 1000,
            utc: time::utc_from_unix(seconds),
            leap_indicator: 0,
            server_ipv4: [192, 0, 2, 1],
            sample_count: 2,
        }
    }

    #[test]
    fn converts_epoch_fraction_and_unsigned_limit_without_overflow() {
        assert_eq!(decode(&snapshot(0, 0).encode()), Ok(0));
        assert_eq!(decode(&snapshot(1_704_067_200, 123).encode()), Ok(1_704_067_200_000_000_123));
        assert_eq!(decode(&snapshot(18_446_744_073, 709_551_615).encode()), Ok(u64::MAX));
        assert_eq!(decode(&snapshot(18_446_744_073, 709_551_616).encode()), Err(OVERFLOW));
        assert_eq!(decode(&snapshot(-1, 999).encode()), Err(OVERFLOW));
        assert_eq!(decode(&snapshot(i64::MAX, 0).encode()), Err(OVERFLOW));
    }

    #[test]
    fn rejects_unavailable_states_and_inconsistent_utc() {
        let mut value = snapshot(1_704_067_200, 0);
        for state in [time::STATE_UNSYNCHRONIZED, time::STATE_HOLDOVER] {
            value.state = state;
            assert_eq!(decode(&value.encode()), Err(UNAVAILABLE));
        }
        value.state = 255;
        assert_eq!(decode(&value.encode()), Err(INVALID_REPLY));
        value.state = time::STATE_SYNCHRONIZED;
        value.utc.day += 1;
        assert_eq!(decode(&value.encode()), Err(INVALID_REPLY));
        value = snapshot(1_704_067_200, 1_000_000_000);
        assert_eq!(decode(&value.encode()), Err(INVALID_REPLY));
        value = snapshot(1_704_067_200, 0);
        value.counter_frequency_hz = 0;
        assert_eq!(decode(&value.encode()), Err(INVALID_REPLY));
    }

    #[test]
    fn sample_preserves_the_service_anchor_rate_and_quality_metadata() {
        let value = snapshot(1_704_067_200, 123);
        assert_eq!(
            decode_sample(&value.encode()),
            Ok(RealtimeSample {
                utc_ns: 1_704_067_200_000_000_123,
                monotonic_ticks: 1000,
                counter_frequency_hz: 1000,
                drift_ppb: -100,
                last_sync_age_ms: 42,
                uncertainty_ms: 20,
                reserved: 0,
            })
        );
        let mut changed = value;
        changed.drift_ppb = -1_000_000_000;
        assert_eq!(decode_sample(&changed.encode()), Err(INVALID_REPLY));
        changed = value;
        changed.last_sync_age_ms = time::NEVER_SYNCED_AGE_MS;
        assert_eq!(decode_sample(&changed.encode()), Err(INVALID_REPLY));
    }

    #[test]
    fn rejects_incomplete_extended_retired_and_noncanonical_frames() {
        let bytes = snapshot(1_704_067_200, 0).encode();
        for length in 0..time::SNAPSHOT_LEN {
            assert_eq!(decode(&bytes[..length]), Err(INVALID_REPLY));
        }
        let mut extended = bytes.to_vec();
        extended.push(0);
        assert_eq!(decode(&extended), Err(INVALID_REPLY));
        for offset in [0, 4, 66, 67, 76, 77, 78, 79] {
            let mut changed = bytes;
            changed[offset] ^= 0xff;
            assert_eq!(decode(&changed), Err(INVALID_REPLY));
        }
    }
}
