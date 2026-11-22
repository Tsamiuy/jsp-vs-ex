// jsp_net_lobby.h -- ENHANCEMENT (EXPERIMENTAL rollback netcode, lobby): open and close a rollback session at RUNTIME instead of only at process start
// from play_settings.txt. Implemented in jsp_net_game.cpp; the same connection code serves the start-up paths (JSP_ROLLBACK_LISTEN / JOIN,
// JSP_ROLLBACK_VIA=repsn + JSP_ROLLBACK_ROLE) and native-room pairing (Online Battle + rollback: the game's own Friendly Match room pairs the players,
// jsp_net_game.cpp native_poll opens the link and closes it with jsp_net_lobby_close), so there is one implementation. jsp_net_native.cpp reads
// jsp_net_lobby_phase. No menu calls jsp_net_lobby_open yet.
//
// Needs a rollback-capable process: JSP_ROLLBACK=1 at start (the snapshot ring, the fight hash and the frame hooks are only set up then). With JSP_ROLLBACK=1 and
// NO role key the start decision (jpno::resolve_env, the lobby outcome) installs everything and opens no session: this API opens one. A start-up role
// session can be closed and reopened with it too. Not with the rollback bench (JSP_ROLLBACK_BENCH).
//
// THREAD: the game's main thread only (the thread that runs the frame hooks, e.g. a menu page drawn from the game loop). Not from the pump or presence threads.
// CONTRACT: open a session from the menus, not while a fight runs: the session starts at the first fight HUD draw after the fight hash anchors again (the
// open resets the fight hash, so a fight still running would be anchored again mid-fight and the two PCs would start on different ticks).
#ifndef JSP_NET_LOBBY_H
#define JSP_NET_LOBBY_H

#ifdef __cplusplus
extern "C" {
#endif

enum { JSP_LOBBY_HOST = 0, JSP_LOBBY_JOIN = 1 };
enum { JSP_LOBBY_DIRECT = 0, JSP_LOBBY_REPSN = 1 };
// What jsp_net_lobby_phase returns (jsp_net_game.cpp Phase): no session / settings handshake (waiting for the other player) / settings agreed, waiting for the
// first fight tick / match running / match ended (the link is still open; a new jsp_net_lobby_open replaces it).
enum { JSP_LOBBY_OFF = 0, JSP_LOBBY_HANDSHAKE = 1, JSP_LOBBY_ARMED = 2, JSP_LOBBY_RUNNING = 3, JSP_LOBBY_ENDED = 4 };

typedef struct JspLobbyCfg {
    int role;                    // JSP_LOBBY_HOST or JSP_LOBBY_JOIN
    int via;                     // JSP_LOBBY_DIRECT (IP address) or JSP_LOBBY_REPSN (a room on the re-psn server of repsn.ini)
    unsigned short port;         // direct host: the UDP port to bind (1..65535). re-psn: an optional fixed UDP port (0 = any). Unused for a direct join.
    const char* join;            // direct join: "a.b.c.d:port" of the host (IPv4 only, as JSP_ROLLBACK_JOIN). Unused otherwise.
    int allow_relay;             // re-psn: 1 = the player allows the server's relay when a direct path fails
    unsigned long long room;     // re-psn join: that room id (0 = the oldest matching room, as JSP_ROLLBACK_ROOM)
} JspLobbyCfg;

// 1 = the connection is opening (settings handshake, then the match starts at the first fight tick, as the start-up path does); 0 = refused, nothing changed
// (the reason: jsp_net_lobby_reason). A session in the handshake, armed or running is refused (close it first); an ended one is closed and replaced.
int jsp_net_lobby_open(const JspLobbyCfg* cfg);
// Ends whatever is open: the other player is told (a handshake Cancel / leave, or the session's disconnect), the socket, the re-psn room and the pump thread go,
// and the state is ready for a new jsp_net_lobby_open. Nothing open: does nothing.
void jsp_net_lobby_close(void);
// Why the last jsp_net_lobby_open returned 0 ("" after a successful one). Valid until the next open. Main thread.
const char* jsp_net_lobby_reason(void);
// JSP_LOBBY_OFF .. JSP_LOBBY_ENDED; any thread (an atomic read).
int jsp_net_lobby_phase(void);

// ENHANCEMENT (same-link rematch): several matches over one open link. Each match has its own settings handshake (new Host / Client objects) under a session id
// made from the link's plan and a match counter both sides keep (jsp_net_match.h); messages of an earlier match are ignored, and a side that is behind follows
// the other one's newer match. A link opened for the game's own Online Battle room (native-room pairing, JSP_ROLLBACK_NATIVE) re-arms by itself after every match
// (and waits until the fight that ended is gone); a link of the start-up role or of jsp_net_lobby_open stays ended unless the caller arms it here. Main thread.
// Arms the next match on the open link: 1 = armed (or already armed, or the link is still meeting the other player: its first match arms by itself); the session
// starts at the next fight's anchor, as today. Call it from the menus (it resets the fight hash, like jsp_net_lobby_open); it also releases a re-armed match that
// still waits for the ended fight's scene to go. 0 = no link, or a match is running (end it first).
int jsp_net_match_begin(void);
// Ends the current match and keeps the link: a running match ends for both (the session's disconnect), a match still in its handshake is cancelled (the other side
// is told). Nothing open: does nothing.
void jsp_net_match_end(void);
// The match counter of the open link (0 = its first match); any thread (a plain read, for logs and display).
unsigned jsp_net_match_index(void);

#ifdef __cplusplus
}
#endif

#endif
