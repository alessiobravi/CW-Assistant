# ADIF conformance policy

CW Assistant targets the current released ADIF specification and is intended to
remain eligible for the conformance or certification process available at the
time of each release. The current baseline is ADIF 3.1.7 (2026-03-22).

## Release rules

- Track the published ADIF release and review specification changes before each
  CW Assistant release. Updating the version is a reviewed code/data change,
  never only a documentation edit.
- Export the recommended QSO identity fields: `QSO_DATE`, `TIME_ON`, `CALL`,
  `MODE`, and `FREQ` and/or `BAND`.
- Enforce field types, enumerations, dependencies, valid date ranges, and the
  distinction between current, import-only/deprecated, and deleted values.
- Never export import-only values. Preserve unknown supported input data where
  the selected storage/export path permits round-tripping.
- Satellite QSOs export `PROP_MODE=SAT`, `SAT_NAME`, `SAT_MODE`, station transmit
  `FREQ`/`BAND`, and station receive `FREQ_RX`/`BAND_RX` as one consistent set.
- Station equipment selected by actual-RF band exports as `MY_RIG` and
  `MY_ANTENNA`. Contacted-station `RIG` is never inferred from local profiles.
- Frequencies are calculated with integer hertz. ADI MHz numbers use six
  fractional digits so transverter conversion does not lose precision.
- ADI record streaming for logger integrations and full ADI file export share
  one validated field model. ADX support will use the official versioned XML
  schema rather than a separately interpreted model.

## Verification

- Unit tests cover field length, escaping/validation, band boundaries, signed
  transverter offsets, split semantics, satellite dependencies, and rejection
  of contradictory or unrepresentable records.
- CI downloads a pinned checksum-verified copy of the official versioned table
  exports, schemas, and test QSO archive. Fixtures are never uploaded to a live
  logging or award service.
- ADX output is schema-validated. ADI output is parsed by an independent test
  parser and round-tripped through the project's parser.
- Release candidates are tested with the configured Log4OM integration, and a
  conformance report records specification version, fixtures, validator output,
  known optional fields, and any certification submission result.

Official references:

- [Current ADIF specification](https://adif.org/adif)
- [ADIF 3.1.7 specification](https://www.adif.org/317/ADIF_317.htm)
- [ADIF resources and implementation notes](https://www.adif.org/317/ADIF_317_Resources.htm)
