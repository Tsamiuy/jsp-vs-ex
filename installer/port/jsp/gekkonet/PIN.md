# GekkoNet (pinned copy)

The rollback session library used by the experimental rollback netcode (`jsp_net_session.cpp`, off unless `JSP_ROLLBACK=1`).

- Upstream: https://github.com/HeatXD/GekkoNet
- Commit: `45dac7e5` ("Fix the default adapter dropping large packets and spinning on errors (#61)")
- Licence: BSD 2-Clause (`LICENSE`, copyright Jamie Meyer); `thirdparty/zpp/zpp_bits.h` is MIT (`thirdparty/zpp/LICENSE`).
- What is here: `GekkoLib/include` as `include/`, `GekkoLib/src` as `src/`, `zpp_bits.h` and its `LICENSE` from `GekkoLib/thirdparty/zpp` as `thirdparty/zpp/`, and the
  top-level `LICENSE`. The files are unchanged except two local additions, each marked "jsp-vs-ex local addition" in the code. The first:
  `gekko_final_frame` (gekkonet.h/.cpp, `FinalFrame` in session.h, game_session.h/.cpp), a read-only accessor for the latest frame no later
  rollback can go back past (from GekkoNet's own confirmed frame and pending incorrect frame). The rollback glue uses it to skip the snapshots
  of states that can never be loaded (`jsp_net_glue.h`). It should go upstream; re-apply it when updating.
  The second: `GekkoNetworkStats` gained `rtt_count` (`NetStats::rtt_count`, +1 in `AddRTT`) and `last_acked_frame` (that player's
  `NetStats::last_acked_frame`), filled in `NetworkStats` of game_session.cpp and spectator_session.cpp. The adaptive input delay keeps only round trips
  whose count is new (`jsp_net_delay_rule.h`), and the match's input-latency summary reads the ack (`jsp_net_perf.h`). Re-apply it when updating. Left out: the asio copy (the port has its own UDP transport and builds with
  `GEKKONET_NO_ASIO`), the examples, the documentation (also zpp_bits' README) and the upstream build files.
- Built by `installer/port/jsp/CMakeLists.txt` as part of the game exe (`GEKKONET_STATIC`, `GEKKONET_NO_ASIO`, `ws2_32`).

To update: copy the same folders from a newer upstream commit, change the commit above, and run the netcode host tests.
