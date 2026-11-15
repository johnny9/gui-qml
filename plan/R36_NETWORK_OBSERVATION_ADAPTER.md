# Network intent and observation adapter

Pinned public Node provides getNetworkActive, setNetworkActive and notifications, without a canonical Core revision or conditional setter. The app must keep queued GUI desire separate from observed networking state.

The Domain command owns its original intent/action scope and actual Native effect/read receipt. Monotonic app callback generations reject stale results and supersede older queued desire. Unknown effects never replay automatically. An owned current Native read remains factual when the notification generation did not advance; a cached no-op is only a hint and still crosses the Native observation fence. Core activity, availability, reachability and peer connectivity remain separate facts.

A captured notification frontier is recorded before exposing its bounded GUI pause projection. A subsequent explicit command therefore captures that actual ingress revision even while the Domain/GUI observation frame is held; Domain still validates the original revision. Backend/subscription availability and command readiness after a current observation have separate typed notifications.

The getter executes without a GUI-shared mailbox lock. Its conservative generation is captured before reading so a notice during a held read can supersede it. GUI projects bounded scalars and narrow commands. Passive facade retirement withdraws publication/ingress while accepted scopes drain effects/outcomes and reserved Native cleanup. Explicit close can cancel known not-started work; presentation loss never proves physical completion.

This adapter does not serialize independent RPC between the app's final generation fence and setter. A concurrent Core/RPC mutation may still run in that interval. The result is an owned observation, not a global atomicity guarantee. No Core implementation or private accessor is changed.

A future public Node API could expose versioned active state and a conditional setter returning actual state/revision plus whether an effect started/applied. Core should own comparison/mutation and define notification ordering. Retire the app generation adapter only after a supported pinned capability satisfies that contract and real external-RPC, queued-successor, current-read, observer-loss, rejection and shutdown controls pass through the consumer. Local state labels cannot supply the missing guarantee.

Current development evidence is under .validation/qt6-r11-r40-20261114/R36/. Original071 is separately accepted; R35 public inspection is focused accepted; whole R36 awaits its repaired-head focused gate. Exhaustive coverage/full matrices/sanitizers remain R42.
