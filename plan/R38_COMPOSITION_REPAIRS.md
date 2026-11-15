# R38 actual desktop endpoint and autocomplete Clear corrections

The immutable second worker candidate passed its six corrected Native rows,
valid-executable QML row, public Core invalidation/reconsider/replacement rows,
pinned tidy and QML static checks. Its remaining failures identify app wiring:

- DesktopWallets supplies separate active wallet name, RPC namespace, readiness
  and availability to SettingsView while hosting wallet settings elsewhere. Its
  `wallet` property remains null. The new endpoint-kind binding incorrectly used
  only that null property, turning a real selected desktop wallet into a node
  console. Add an explicit typed `activeWalletRpcContext` binding alongside those
  existing scalar facts. Endpoint kind remains independent of an empty/default
  wallet name, and unavailable wallet facts refuse ordinary input. This is an
  ordinary app composition correction; no missing Core API is inferred.
- Autocomplete uses the existing non-modal native ContextMenu with CloseOnEscape.
  [Qt6.10.2's context matcher](https://raw.githubusercontent.com/qt/qtdeclarative/v6.10.2/src/quicktemplates/qquickshortcutcontext.cpp)
  blocks outside window shortcuts while such a popup is open. The existing Clear
  shortcut therefore cannot fire during the original busy/autocomplete schedule.
  Keep the normal window shortcut and register the same narrow command inside
  the existing popup, with mutually exclusive visibility guards. Other popups
  retain Qt's blocking policy. No application-wide shortcut, Qt patch, custom
  component, animation disable or timeout change is introduced.

Five new public component rows cover named/default/unavailable scalar endpoint
consumers and physical ordinary/autocomplete Clear with exact command counts,
unchanged draft and no RPC submission. The autocomplete row also closes the real
menu and repeats the physical ordinary shortcut, proving a single owner per
keystroke across its exit. Existing Core rows preserve the original held RPC,
exact generation increment, original immutable result and physical key events;
explicit page endpoint and opened-popup assertions establish their premises.
All prior failures remain retained, with no source snapshot overwritten.

R36/R39 composition is isolated and must receive its final focused runtime gate.
Exhaustive coverage, platform matrices and sanitizers remain R42 obligations.

The third immutable worker run passed all eight Core rows and both actual GUI
privacy scenarios, along with seven of nine selected QML rows and all static
checks. Only the new two shortcut QML rows failed: their console was parented
under the invisible TestCase item. [Qt6.10.2 TestCase itself defaults to hidden](https://raw.githubusercontent.com/qt/qtdeclarative/v6.10.2/src/qmltest/TestCase.qml).
Parent the temporary console to the actual test window content item and assert
its window and effective visibility before physical input. This changes the
fixture premise only; production code, exact Clear counts/draft/no-RPC assertions,
animations, key events and watchdogs remain unchanged. The actual Core Clear
cases already pass. The reduced next check runs these two rows only; positive
Core/process/static results retain their exact source/ELF receipts.


## Retained-draft Escape routing

Worker04 established both physical Clear paths after the visible-parent fixture
repair: ordinary passed, and autocomplete reached the exact first Clear effect
with unchanged draft and command count. Its additional Escape continuation then
failed because the native popup stayed visible. This is an application focus
configuration mismatch: autocomplete deliberately has `focus: false` so typing
continues in the draft, while Qt's `Popup.CloseOnEscape` requires active popup
focus ([official Popup contract](https://doc.qt.io/qt-6/qml-qtquick-controls-popup.html#closePolicy-prop)).

The focused field now routes Escape to its existing popup's `close()` command,
only while it is visible. Otherwise the event remains unaccepted. No Qt/native
component replacement, popup focus transfer, RPC effect, animation, watchdog or
warning-policy change is involved. The original physical Escape/closed-popup/
second Clear oracles remain, strengthened with unchanged draft, retained input
focus and no RPC after Escape. Worker04's actual failed row remains evidence;
it is not reclassified as a pass. This presentation issue does not require a
new public Core capability.
