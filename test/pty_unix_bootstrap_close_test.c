/*
 * A close that finishes while bootstrap is between the RUNNING transition and
 * its SPAWNED post must not post SESSION_CLOSED first: Dart releases its
 * reference and closes the port on SESSION_CLOSED, so a later post would fail,
 * abandon the session and release that reference a second time.
 */
#include <assert.h>
#include <stdatomic.h>
#include <unistd.h>

#include "../src/flutter_pty.h"
#include "../src/include/dart_api_dl.h"
#include "../src/pty_internal.h"

static _Atomic int port_closed;
static _Atomic int session_closed;
static _Atomic int posts_after_close;

static bool post_object(Dart_Port_DL port, Dart_CObject *message)
{
    (void)port;
    const int32_t type = message->value.as_array.values[0]->value.as_int32;
    // Hold bootstrap between the RUNNING transition and its SPAWNED post.
    if (type == PTY_EVENT_SPAWNED) usleep(500 * 1000);
    if (atomic_load(&port_closed)) {
        atomic_fetch_add(&posts_after_close, 1);
        return false;
    }
    if (type == PTY_EVENT_SESSION_CLOSED) atomic_store(&session_closed, 1);
    return true;
}

int main(void)
{
    Dart_PostCObject_DL = post_object;
    const char *arguments[] = {"-c", "sleep 5"};
    const char *environment[] = {"PATH=/usr/bin:/bin"};
    const PtySpawnOptions options = {
        .executable = "/bin/sh",
        .arguments = arguments,
        .argument_count = 2,
        .environment = environment,
        .environment_count = 1,
        .size = {.rows = 24, .columns = 80},
        .input_buffer_bytes = 64 * 1024,
        .output_window_bytes = 64 * 1024,
        .event_port = 1,
    };
    PtySession *session = NULL;
    PtyError error;
    assert(pty_session_start(&options, &session, &error) == 1);
    while (atomic_load(&session->lifecycle) != PTY_LIFECYCLE_RUNNING) {
        usleep(1000);
    }
    pty_session_begin_close(session);
    while (!atomic_load(&session_closed)) usleep(1000);
    // What Dart does on SESSION_CLOSED: release its reference, close the port.
    pty_session_release(session);
    atomic_store(&port_closed, 1);
    sleep(1);

    assert(atomic_load(&posts_after_close) == 0);
    PtyDebugStats stats;
    pty_debug_get_stats(&stats);
    assert(stats.live_sessions == 0);
    return 0;
}
