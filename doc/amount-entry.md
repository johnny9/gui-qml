# Exact amount entry

Send and Receive use the same bounded native parser and BitcoinAmount input value.
QML binds typed draft text, validation, errors and dirty state and sends edit,
finish, reset or commit commands. Monetary parsing does no JavaScript arithmetic,
character stripping, fractional truncation or floating conversion.

The parser accepts canonical dot and the numeric locale's decimal mark. Decimal
marks are never grouping separators. It uses checked integer accumulation for all
four units, rejects grouping/junk/signs/excess precision and values above MAX_MONEY,
and distinguishes Empty, Incomplete, Invalid and Valid. Parsing examines at most128
UTF-16 code units. An oversized edit retains at most129 code units as a visibly
invalid draft; it cannot become a valid truncated amount. Canonical display values
also serve signed read-only displays; committing an edited monetary quantity checks
MoneyRange separately.

Invalid/incomplete edits retain the last canonical draft value and visible text,
but cannot be committed. Send recipient validation and final preparation observe
that state even when canonical satoshis are unchanged. A presentation-unit change
preserves satoshis. A valid pending Receive edit changes its representation without
changing the saved request until Update. Invalid text stays invalid on a global
unit change. Optional Receive zero is explicitly presented as empty.

Receive owns a local input value and binds its canonical source while untouched.
A separate dirty notification fires only when the flag changes, so resetting the
bound source cannot reenter its binding condition. Save/Update validates before
copying canonical data. Send binds directly to its existing amount owner. Existing
payment-URI provenance and explicit maximum/fee modes keep their own owners;
this input value does not infer them from numeric equality. Full Send and Receive
ownership migrations remain R16/R30, including retained issue295 regression cases.

Input validation/formatting is bounded presentation-value work and introduces no
Core calls, filesystem work, thread, clock or asynchronous amount workflow. Tests
cover exact parsing, state/notification/commit boundaries, real typing, clipboard
paste and IME commit in all units. Independent Core oracles decode the persisted
receive record and prepared send transaction; no transaction is broadcast.
