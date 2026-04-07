# ReaClaw API Reference

Full API specification is in `ReaClaw_Design.md` §4. This file will be kept in sync
with the implemented endpoints as each phase is completed.

## Phase 0 (v0.1.0) — implemented endpoints

_Not yet implemented — in progress._

---

## Error format

All errors return:
```json
{ "error": "description", "code": "ERROR_CODE", "context": {} }
```

HTTP status codes: `200`, `400`, `401`, `404`, `408`, `500`
