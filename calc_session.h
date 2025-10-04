#ifndef CALC_SESSION_H
#define CALC_SESSION_H

#include <pthread.h>
#include "ticables.h"
#include "ticalcs.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    APP_OK = 0,
    APP_ERR_NO_CALC = 1,
    APP_ERR_NO_CABLE,
    APP_ERR_ALLOC,
    APP_ERR_NOT_READY,
    APP_ERR_IO,
    APP_ERR_THREAD,
    APP_ERR_CRYPTO
};

typedef struct
{
    CableHandle *cable;
    CalcHandle *calc;
    CableModel cable_model;
    CalcModel calc_model;
    CablePort port_number;
    int poll_interval_ms;
    volatile int poll_active;
    int poll_thread_started;
    pthread_t poll_thread;
} CalcSession;

int calc_session_open(CalcSession *session);
void calc_session_cleanup(CalcSession *session);
int calc_session_start_polling(CalcSession *session, int interval_ms);
void calc_session_stop_polling(CalcSession *session);

#ifdef __cplusplus
}
#endif

#endif
