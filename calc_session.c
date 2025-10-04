#include <stdio.h>
#include <unistd.h>
#include "calc_session.h"

#define CALC_SESSION_MAX_POLL_CYCLES 3600000u

static void *calc_session_polling_thread(void *arg);

int calc_session_open(CalcSession *session)
{
    CableDeviceInfo *devices = NULL;
    int device_count = 0;
    int status = APP_ERR_NO_CALC;

    if (session != NULL)
    {
        ticables_get_usb_device_info(&devices, &device_count);
        if (device_count < 1)
        {
            fprintf(stderr, "No USB calculator detected\n");
            status = APP_ERR_NO_CALC;
        }
        else if (devices[0].family == CABLE_FAMILY_UNKNOWN)
        {
            fprintf(stderr, "Unsupported cable family\n");
            status = APP_ERR_NO_CABLE;
        }
        else
        {
            status = APP_OK;
            session->cable_model = (devices[0].family == CABLE_FAMILY_DBUS) ? CABLE_SLV : CABLE_USB;
            session->calc_model = ticalcs_device_info_to_model(&devices[0]);
            session->calc_model = ticalcs_remap_model_from_usb(session->cable_model, session->calc_model);

            printf("Detected calculator model: %s\n", ticalcs_model_to_string(session->calc_model));
            if (session->calc_model == CALC_NONE)
            {
                int probe_status = ticalcs_probe(session->cable_model, session->port_number, &session->calc_model, 1);
                if (probe_status != 0)
                {
                    fprintf(stderr, "No calculator found during probe\n");
                    status = APP_ERR_NO_CALC;
                }
                else
                {
                    printf("Probed calculator model: %s\n", ticalcs_model_to_string(session->calc_model));
                }
            }
            else if (session->calc_model != CALC_TI83P)
            {
                fprintf(stderr, "Warning: detected model is not TI-83 Plus\n");
            }

            if (status == APP_OK)
            {
                session->cable = ticables_handle_new(session->cable_model, session->port_number);
                if (session->cable == NULL)
                {
                    fprintf(stderr, "ticables_handle_new failed\n");
                    status = APP_ERR_NO_CABLE;
                }
            }

            if (status == APP_OK)
            {
                session->calc = ticalcs_handle_new(session->calc_model);
                if (session->calc == NULL)
                {
                    fprintf(stderr, "ticalcs_handle_new failed\n");
                    status = APP_ERR_ALLOC;
                }
            }

            if (status == APP_OK)
            {
                if (ticalcs_cable_attach(session->calc, session->cable) != 0)
                {
                    fprintf(stderr, "ticalcs_cable_attach failed\n");
                    status = APP_ERR_NO_CABLE;
                }
            }

            if (status == APP_OK)
            {
                ticables_options_set_timeout(session->cable, 250);
            }
        }
    }

    if (devices != NULL)
    {
        ticables_free_usb_device_info(devices);
    }

    if ((status != APP_OK) && (session != NULL))
    {
        calc_session_cleanup(session);
    }

    return status;
}

void calc_session_cleanup(CalcSession *session)
{
    if (session != NULL)
    {
        if (session->calc != NULL)
        {
            ticalcs_handle_del(session->calc);
            session->calc = NULL;
        }

        if (session->cable != NULL)
        {
            ticables_handle_del(session->cable);
            session->cable = NULL;
        }

        session->poll_active = 0;
        session->poll_thread_started = 0;
    }
}

int calc_session_start_polling(CalcSession *session, int interval_ms)
{
    int status = APP_ERR_NO_CALC;

    if ((session != NULL) && (session->calc != NULL))
    {
        session->poll_interval_ms = interval_ms;
        session->poll_active = 1;
        if (pthread_create(&session->poll_thread, NULL, calc_session_polling_thread, session) != 0)
        {
            session->poll_active = 0;
            status = APP_ERR_THREAD;
        }
        else
        {
            session->poll_thread_started = 1;
            status = APP_OK;
        }
    }

    return status;
}

void calc_session_stop_polling(CalcSession *session)
{
    if (session != NULL)
    {
        if (session->poll_thread_started != 0)
        {
            session->poll_active = 0;
            pthread_join(session->poll_thread, NULL);
            session->poll_thread_started = 0;
        }
    }
}

static void *calc_session_polling_thread(void *arg)
{
    CalcSession *session = (CalcSession *)arg;
    unsigned int cycle_count = 0u;
    int last_ready = -1;

    if (session == NULL)
    {
        return NULL;
    }

    while ((session->poll_active != 0) && (cycle_count < CALC_SESSION_MAX_POLL_CYCLES))
    {
        if (session->calc != NULL)
        {
            int ready = ticalcs_calc_isready(session->calc);
            if ((ready == 0) && (last_ready != 1))
            {
                printf("[poll] Calculator ready\n");
                fflush(stdout);
                last_ready = 1;
            }
            else if ((ready != 0) && (last_ready != 0))
            {
                printf("[poll] Calculator not ready\n");
                fflush(stdout);
                last_ready = 0;
            }
        }

        usleep((useconds_t)(session->poll_interval_ms * 1000));
        cycle_count++;
    }

    session->poll_active = 0;

    return NULL;
}
