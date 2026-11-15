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
