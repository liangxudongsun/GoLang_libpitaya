/**
 * Copyright (c) 2014,2015 NetEase, Inc. and other Pomelo contributors
 * MIT Licensed.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <pomelo.h>
#include <pomelo_trans.h>

#include "test_common.h"

static pc_client_t* g_client = NULL;

static bool g_event_cb_called = false;
static bool g_request_cb_called = false;
static bool g_notify_cb_called = false;

static void *
setup(const MunitParameter params[], void *data)
{
    Unused(data); Unused(params);
    // NOTE: use calloc in order to avoid one of the issues with the api.
    // see `issues.md`.
    g_client = calloc(1, pc_client_size());
    assert_not_null(g_client);
    return NULL;
}

static void
teardown(void *data)
{
    Unused(data);
    free(g_client);
    g_client = NULL;
}

static int EV_ORDER[] = {
    PC_EV_CONNECTED,
    PC_EV_DISCONNECT,
};

static void
event_cb(pc_client_t* client, int ev_type, void* ex_data, const char* arg1, const char* arg2)
{
    int *num_called = ex_data;
    assert_int(ev_type, ==, EV_ORDER[*num_called]);
    (*num_called)++;
}

static void
request_cb(const pc_request_t* req, int rc, const char* resp)
{
    bool *called = pc_request_ex_data(req);
    *called = true;

    assert_string_equal(resp, EMPTY_RESP);
    assert_int(rc, ==, PC_RC_OK);
    assert_not_null(resp);
    assert_ptr_equal(pc_request_client(req), g_client);
    assert_string_equal(pc_request_route(req), REQ_ROUTE);
    assert_string_equal(pc_request_msg(req), REQ_MSG);
    assert_int(pc_request_timeout(req), ==, REQ_TIMEOUT);
}

static void
notify_cb(const pc_notify_t* noti, int rc)
{
    bool *called = pc_notify_ex_data(noti);
    *called = true;

    assert_int(rc, ==, PC_RC_OK);
    assert_ptr_equal(pc_notify_client(noti), g_client);
    assert_string_equal(pc_notify_route(noti), NOTI_ROUTE);
    assert_string_equal(pc_notify_msg(noti), NOTI_MSG);
    assert_int(pc_notify_timeout(noti), ==, NOTI_TIMEOUT);
}

static MunitResult
test_successful_handshake(const MunitParameter params[], void *state)
{
    Unused(state); Unused(params);
    pc_client_config_t config = PC_CLIENT_CONFIG_DEFAULT;
    config.transport_name = PC_TR_NAME_UV_TLS;

    int num_ev_cb_called = 0;
    bool req_cb_called = false;
    bool noti_cb_called = false;

    assert_int(pc_client_init(g_client, NULL, &config), ==, PC_RC_OK);

    int handler_id = pc_client_add_ev_handler(g_client, event_cb, &num_ev_cb_called, NULL);
    assert_int(handler_id, !=, PC_EV_INVALID_HANDLER_ID);

    // Set CA file so that the handshake is successful.
    assert_true(tr_uv_tls_set_ca_file("../../test/server/fixtures/ca.crt", NULL));

    assert_int(pc_client_connect(g_client, LOCALHOST, g_test_server.tls_port, NULL), ==, PC_RC_OK);
    SLEEP_SECONDS(1);
    assert_int(pc_request_with_timeout(g_client, REQ_ROUTE, REQ_MSG, &req_cb_called, REQ_TIMEOUT, request_cb, NULL), ==, PC_RC_OK);
    assert_int(pc_notify_with_timeout(g_client, NOTI_ROUTE, NOTI_MSG, &noti_cb_called, NOTI_TIMEOUT, notify_cb), ==, PC_RC_OK);
    SLEEP_SECONDS(2);

    assert_true(noti_cb_called);
    assert_true(req_cb_called);

    assert_int(pc_client_disconnect(g_client), ==, PC_RC_OK);
    SLEEP_SECONDS(1);

    assert_int(num_ev_cb_called, ==, ArrayCount(EV_ORDER));
    assert_int(pc_client_rm_ev_handler(g_client, handler_id), ==, PC_RC_OK);
    assert_int(pc_client_cleanup(g_client), ==, PC_RC_OK);
    return MUNIT_OK;
}


static void
connect_failed_event_cb(pc_client_t* client, int ev_type, void* ex_data, const char* arg1, const char* arg2)
{
    Unused(client);

    bool *called = ex_data;
    *called = true;
    assert_int(ev_type, ==, PC_EV_CONNECT_FAILED);
    assert_string_equal(arg1, "TLS Handshake Error");
    assert_null(arg2);
}

static void
test_invalid_handshake()
{
    // The client fails the connection when no certificate files are set
    // with the function tr_uv_tls_set_ca_file.
    bool called = false;
    int handler_id = pc_client_add_ev_handler(g_client, connect_failed_event_cb, &called, NULL);
    assert_int(handler_id, !=, PC_EV_INVALID_HANDLER_ID);

    assert_int(pc_client_connect(g_client, LOCALHOST, g_test_server.tls_port, NULL), ==, PC_RC_OK);
    SLEEP_SECONDS(1);

    assert_true(called);
    assert_int(pc_client_disconnect(g_client), ==, PC_RC_INVALID_STATE);
    assert_int(pc_client_rm_ev_handler(g_client, handler_id), ==, PC_RC_OK);
}

static MunitResult
test_no_client_certificate(const MunitParameter params[], void *state)
{
    Unused(state); Unused(params);
    pc_client_config_t config = PC_CLIENT_CONFIG_DEFAULT;
    config.transport_name = PC_TR_NAME_UV_TLS;
    assert_int(pc_client_init(g_client, NULL, &config), ==, PC_RC_OK);

    // Without setting a CA file, the handshake should fail.
    test_invalid_handshake();
    // Setting an unexistent CA file should fail the function and also fail the handshake.
    assert_false(tr_uv_tls_set_ca_file("./this/ca/does/not/exist.crt", NULL));
    test_invalid_handshake();

    assert_int(pc_client_cleanup(g_client), ==, PC_RC_OK);
    return MUNIT_OK;
}

static MunitResult
test_wrong_client_certificate(const MunitParameter params[], void *state)
{
    Unused(state); Unused(params);
    pc_client_config_t config = PC_CLIENT_CONFIG_DEFAULT;
    config.transport_name = PC_TR_NAME_UV_TLS;
    assert_int(pc_client_init(g_client, NULL, &config), ==, PC_RC_OK);

    // Setting the WRONG CA file should not fail the function but make the handshake fail.
    assert_true(tr_uv_tls_set_ca_file("../../test/server/fixtures/ca_incorrect.crt", NULL));
    test_invalid_handshake();

    assert_int(pc_client_cleanup(g_client), ==, PC_RC_OK);
    return MUNIT_OK;
}

static MunitTest tests[] = {
    {"/no_client_certificate", test_no_client_certificate, setup, teardown, MUNIT_TEST_OPTION_NONE, NULL},
    {"/wrong_client_certificate", test_wrong_client_certificate, setup, teardown, MUNIT_TEST_OPTION_NONE, NULL},
    {"/successful_handshake", test_successful_handshake, setup, teardown, MUNIT_TEST_OPTION_NONE, NULL},
    {NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL},
};

const MunitSuite tls_suite = {
    "/tls", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE
};
