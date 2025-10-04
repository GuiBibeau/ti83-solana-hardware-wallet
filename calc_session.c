#include <stdio.h>
#include <unistd.h>
#include "calc_session.h"

static void *calc_session_polling_thread(void *arg);

int calc_session_open(CalcSession *session)
{
    CableDeviceInfo *devices = NULL;
    int ndevices = 0;
    int err = APP_OK;

    if (session == NULL)
    {
        return APP_ERR_NO_CALC;
    }

    ticables_get_usb_device_info(&devices, &ndevices);
    if (ndevices < 1)
    {
        fprintf(stderr, "No USB calculator detected\n");
        err = APP_ERR_NO_CALC;
        goto cleanup;
    }

    if (devices[0].family == CABLE_FAMILY_UNKNOWN)
    {
        fprintf(stderr, "Unsupported cable family\n");
        err = APP_ERR_NO_CABLE;
        goto cleanup;
    }

    session->cable_model = (devices[0].family == CABLE_FAMILY_DBUS) ? CABLE_SLV : CABLE_USB;
    session->calc_model = ticalcs_device_info_to_model(&devices[0]);
    session->calc_model = ticalcs_remap_model_from_usb(session->cable_model, session->calc_model);

    printf("Detected calculator model: %s\n", ticalcs_model_to_string(session->calc_model));
    if (session->calc_model == CALC_NONE)
    {
        int probe_ret;
        fprintf(stderr, "Falling back to probe for calculator model\n");
        probe_ret = ticalcs_probe(session->cable_model, session->port_number, &session->calc_model, 1);
        if (probe_ret)
        {
            fprintf(stderr, "No calculator found during probe\n");
            err = APP_ERR_NO_CALC;
            goto cleanup;
        }
        printf("Probed calculator model: %s\n", ticalcs_model_to_string(session->calc_model));
    }
    else if (session->calc_model != CALC_TI83P)
    {
        fprintf(stderr, "Warning: detected model is not TI-83 Plus\n");
    }

    session->cable = ticables_handle_new(session->cable_model, session->port_number);
    if (session->cable == NULL)
    {
        fprintf(stderr, "ticables_handle_new failed\n");
        err = APP_ERR_NO_CABLE;
        goto cleanup;
    }

    session->calc = ticalcs_handle_new(session->calc_model);
    if (session->calc == NULL)
    {
        fprintf(stderr, "ticalcs_handle_new failed\n");
        err = APP_ERR_ALLOC;
        goto cleanup;
    }

    if (ticalcs_cable_attach(session->calc, session->cable) != 0)
    {
        fprintf(stderr, "ticalcs_cable_attach failed\n");
        err = APP_ERR_NO_CABLE;
        goto cleanup;
    }

    ticables_options_set_timeout(session->cable, 250);

cleanup:
    if (devices)
    {
        ticables_free_usb_device_info(devices);
    }
    if (err != APP_OK)
    {
        calc_session_cleanup(session);
    }

    return err;
}

void calc_session_cleanup(CalcSession *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->calc)
    {
        ticalcs_handle_del(session->calc);
        session->calc = NULL;
    }

    if (session->cable)
    {
        ticables_handle_del(session->cable);
        session->cable = NULL;
    }

    session->poll_active = 0;
    session->poll_thread_started = 0;
}

int calc_session_start_polling(CalcSession *session, int interval_ms)
{
    if (session == NULL || session->calc == NULL)
    {
        return APP_ERR_NO_CALC;
    }

    session->poll_interval_ms = interval_ms;
    session->poll_active = 1;
    if (pthread_create(&session->poll_thread, NULL, calc_session_polling_thread, session) != 0)
    {
        session->poll_active = 0;
        return APP_ERR_THREAD;
    }

    session->poll_thread_started = 1;
    return APP_OK;
}

void calc_session_stop_polling(CalcSession *session)
{
    if (session == NULL)
    {
        return;
    }

    if (session->poll_thread_started)
    {
        session->poll_active = 0;
        pthread_join(session->poll_thread, NULL);
        session->poll_thread_started = 0;
    }
}

static void *calc_session_polling_thread(void *arg)
{
    CalcSession *session = (CalcSession *)arg;
    int last_ready = -1;

    while (session->poll_active)
    {
        if (session->calc)
        {
            int ready = ticalcs_calc_isready(session->calc);
            if (ready == 0 && last_ready != 1)
            {
                printf("[poll] Calculator ready\n");
                fflush(stdout);
                last_ready = 1;
            }
            else if (ready != 0 && last_ready != 0)
            {
                printf("[poll] Calculator not ready\n");
                fflush(stdout);
                last_ready = 0;
            }
        }

        usleep((useconds_t)(session->poll_interval_ms * 1000));
    }

    return NULL;
}
