#include "titan_session_service.h"
#include <string.h>

typedef struct TitanSessionServiceState {
    TitanSessionState state;
    TitanSessionDescriptor descriptor;
    TitanSessionMetrics metrics;
    uint64_t last_update_ms;
} TitanSessionServiceState;

static TitanSessionServiceState g_session;

static int TitanSession_TransitionAllowed(TitanSessionState from, TitanSessionState to)
{
    switch (from) {
        case TITAN_SESSION_IDLE: return to == TITAN_SESSION_PREPARING;
        case TITAN_SESSION_PREPARING: return to == TITAN_SESSION_LAUNCHING || to == TITAN_SESSION_FAILED;
        case TITAN_SESSION_LAUNCHING: return to == TITAN_SESSION_RUNNING || to == TITAN_SESSION_FAILED;
        case TITAN_SESSION_RUNNING: return to == TITAN_SESSION_OVERLAY_ACTIVE || to == TITAN_SESSION_RETURN_REQUESTED || to == TITAN_SESSION_FAILED;
        case TITAN_SESSION_OVERLAY_ACTIVE: return to == TITAN_SESSION_RUNNING || to == TITAN_SESSION_RETURN_REQUESTED || to == TITAN_SESSION_FAILED;
        case TITAN_SESSION_RETURN_REQUESTED: return to == TITAN_SESSION_RETURNING || to == TITAN_SESSION_RUNNING || to == TITAN_SESSION_FAILED;
        case TITAN_SESSION_RETURNING: return to == TITAN_SESSION_IDLE || to == TITAN_SESSION_FAILED;
        case TITAN_SESSION_FAILED: return to == TITAN_SESSION_RECOVERING || to == TITAN_SESSION_IDLE;
        case TITAN_SESSION_RECOVERING: return to == TITAN_SESSION_IDLE || to == TITAN_SESSION_FAILED;
        default: return 0;
    }
}

void TitanSession_Initialize(void)
{
    memset(&g_session, 0, sizeof(g_session));
    g_session.state = TITAN_SESSION_IDLE;
}

void TitanSession_Shutdown(void)
{
    /* Power/recovery boundary: no session state may survive the Titan process. */
    memset(&g_session, 0, sizeof(g_session));
    g_session.state = TITAN_SESSION_IDLE;
}

int TitanSession_Transition(TitanSessionState next)
{
    if (!TitanSession_TransitionAllowed(g_session.state, next)) {
        g_session.metrics.rejected_transition_count++;
        return 0;
    }
    g_session.state = next;
    g_session.metrics.transition_count++;
    if (next == TITAN_SESSION_OVERLAY_ACTIVE) g_session.metrics.overlay_open_count++;
    if (next == TITAN_SESSION_RETURN_REQUESTED) g_session.metrics.return_request_count++;
    if (next == TITAN_SESSION_RECOVERING) g_session.metrics.recovery_count++;
    if (next == TITAN_SESSION_IDLE) {
        memset(&g_session.descriptor, 0, sizeof(g_session.descriptor));
        g_session.last_update_ms = 0;
    }
    return 1;
}

int TitanSession_Begin(const TitanSessionDescriptor *descriptor)
{
    if (!descriptor || g_session.state != TITAN_SESSION_IDLE || descriptor->engine == TITAN_SESSION_ENGINE_NONE) return 0;
    g_session.descriptor = *descriptor;
    g_session.metrics.begin_count++;
    g_session.last_update_ms = descriptor->start_time_ms;
    return TitanSession_Transition(TITAN_SESSION_PREPARING);
}

int TitanSession_RequestReturn(void)
{
    if (!TitanSession_CanReturn()) return 0;
    return TitanSession_Transition(TITAN_SESSION_RETURN_REQUESTED);
}

int TitanSession_EnterOverlay(void)
{
    if (g_session.state != TITAN_SESSION_RUNNING || !TitanSession_CanOverlay()) return 0;
    return TitanSession_Transition(TITAN_SESSION_OVERLAY_ACTIVE);
}

int TitanSession_Resume(void)
{
    if (g_session.state != TITAN_SESSION_OVERLAY_ACTIVE && g_session.state != TITAN_SESSION_RETURN_REQUESTED) return 0;
    return TitanSession_Transition(TITAN_SESSION_RUNNING);
}

void TitanSession_Fail(void)
{
    if (g_session.state != TITAN_SESSION_IDLE && g_session.state != TITAN_SESSION_FAILED)
        (void)TitanSession_Transition(TITAN_SESSION_FAILED);
}

void TitanSession_BeginRecovery(void)
{
    if (g_session.state == TITAN_SESSION_FAILED)
        (void)TitanSession_Transition(TITAN_SESSION_RECOVERING);
}

void TitanSession_End(void)
{
    if (g_session.state == TITAN_SESSION_RETURNING || g_session.state == TITAN_SESSION_FAILED || g_session.state == TITAN_SESSION_RECOVERING)
        (void)TitanSession_Transition(TITAN_SESSION_IDLE);
}

void TitanSession_Update(uint64_t now_ms)
{
    if (g_session.state == TITAN_SESSION_IDLE) return;
    g_session.last_update_ms = now_ms;
    g_session.metrics.update_cost_units++;
}

TitanSessionState TitanSession_GetState(void) { return g_session.state; }
const TitanSessionDescriptor *TitanSession_GetDescriptor(void) { return g_session.state == TITAN_SESSION_IDLE ? 0 : &g_session.descriptor; }
const TitanSessionMetrics *TitanSession_GetMetrics(void) { return &g_session.metrics; }
int TitanSession_IsActive(void) { return g_session.state != TITAN_SESSION_IDLE; }

uint64_t TitanSession_GetElapsedMs(uint64_t now_ms)
{
    if (g_session.state == TITAN_SESSION_IDLE || now_ms < g_session.descriptor.start_time_ms) return 0;
    return now_ms - g_session.descriptor.start_time_ms;
}

int TitanSession_CanOverlay(void)
{
    uint32_t caps;
    if (g_session.descriptor.igr_compatibility == TITAN_IGR_UNKNOWN ||
        g_session.descriptor.igr_compatibility == TITAN_IGR_DISABLED ||
        g_session.descriptor.igr_compatibility == TITAN_IGR_INCOMPATIBLE) return 0;
    caps = g_session.descriptor.runtime_capabilities;
    return (caps & (TITAN_SESSION_CAP_OVERLAY_MINIMAL | TITAN_SESSION_CAP_OVERLAY_FULL)) != 0u &&
           (caps & TITAN_SESSION_CAP_GS_STATE_RESTORE) != 0u;
}

int TitanSession_CanReturn(void)
{
    if (g_session.descriptor.igr_compatibility == TITAN_IGR_DISABLED ||
        g_session.descriptor.igr_compatibility == TITAN_IGR_INCOMPATIBLE) return 0;
    if (g_session.descriptor.igr_compatibility == TITAN_IGR_UNKNOWN &&
        g_session.descriptor.overlay_level != TITAN_OVERLAY_RETURN_ONLY) return 0;
    return (g_session.descriptor.runtime_capabilities & TITAN_SESSION_CAP_SAFE_RETURN) != 0u;
}
