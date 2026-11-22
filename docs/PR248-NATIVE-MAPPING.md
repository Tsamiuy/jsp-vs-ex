# Native fight ownership

The rollback session assigns one actor to each port from zero through `SessionPlan.total_players - 1`. The local range and remote ranges determine who owns those ports. Native admission checks the room before it arms the replacement pad reader. The occupied human room records must cover exactly those ports, and each planned human must have one battle fighter with a unique net slot and matching member ID. The fighter's placement may differ from its room-record order. CPU records with a nonzero member field remain excluded.

The local fighter must also map to this machine's planned port. A fresh session rebuilds the map from its current battle and room records. The host test covers missing participants, extra humans, stale members and all 33 placement permutations across one to four ports. Runtime validation still needs two machines comparing final setup and observing the fighter that actually responds to each controller. Pad serialization counters alone cannot establish either observation.

The canonical pad-info hook uses the toolkit CellPadInfo2 declaration and callback type. The production target and syntax checker include the same input headers. This removes a mismatched callback declaration and duplicate record layout; it does not establish a prior observed controller failure on the target ABI.
