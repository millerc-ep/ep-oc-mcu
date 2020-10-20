/**
 * OneEdgeService.cpp
 *
 * Built with ARM Mbed-OS
 *
 * Copyright (c) 2020 Embedded Planet, Inc.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include "OneEdgeService.h"

#define TRACE_GROUP   "1EDG"

OneEdgeService::OneEdgeService()
{
    dev = CellularDevice::get_target_default_instance();
    at_handler = dev->get_at_handler();

    at_handler->set_urc_handler("LWM2M-TLT:", mbed::Callback<void()>(this, &OneEdgeService::urc_lwm2m_tlt));
    at_handler->set_urc_handler("#LWM2MRING:", mbed::Callback<void()>(this, &OneEdgeService::urc_lwm2mring));
    at_handler->set_urc_handler("#LWM2MEND:", mbed::Callback<void()>(this, &OneEdgeService::urc_lwm2mend));
    at_handler->set_urc_handler("#LWM2MINFO:", mbed::Callback<void()>(this, &OneEdgeService::urc_lwm2minfo));
}

void OneEdgeService::init()
{
    // Check if the device is ready
    if (dev->is_ready() != NSAPI_ERROR_OK) {
        dev->soft_power_on();
        ThisThread::sleep_for(10000);
        dev->init();
    }

    if (!is_ipeasy_context_activated(1)) {
        activate_ipeasy_context(1);
    }
}

bool OneEdgeService::is_ipeasy_context_activated(int context_id)
{
    at_handler->lock();

    at_handler->cmd_start_stop("#SGACT?", "");
    at_handler->resp_start("#SGACT:");

    int current_context_id = -1;
    int current_stat = -1;

    for (int i = 0; i < ME310_CONTEXT_MAX; i++) {
        current_context_id = at_handler->read_int();
        current_stat = at_handler->read_int();

        if (current_context_id == context_id) {
            at_handler->resp_stop();
            at_handler->unlock();
            return current_stat == ME310_IPEASY_ACTIVATED_CONTEXT;
        }
    }

    at_handler->resp_stop();
    at_handler->unlock();
    return false;
}

nsapi_error_t OneEdgeService::activate_ipeasy_context(int context_id)
{
    at_handler->lock();

    at_handler->at_cmd_discard("#SGACT", "=", "%d%d", context_id, ME310_IPEASY_ACTIVATED_CONTEXT);

    return at_handler->unlock_return_error();
}

nsapi_error_t OneEdgeService::lwm2m_client_enable(int context, AckModaliy mode)
{
    at_handler->lock();

    at_handler->at_cmd_discard("#LWM2MENA", "=", "%d%d%d", ONEEDGE_CLIENT_ENABLED, context, mode);

    return at_handler->unlock_return_error();
}

bool OneEdgeService::lwm2m_client_is_enabled()
{
    int client_state = 0;

    at_handler->lock();

    at_handler->at_cmd_int("#LWM2MENA", "?", client_state);

    nsapi_error_t err = at_handler->unlock_return_error();

    if (err != NSAPI_ERROR_OK) {
        return false;
    }

    return client_state == ONEEDGE_CLIENT_ENABLED;
}

nsapi_error_t OneEdgeService::lwm2m_client_set_battery_level(int battery_level)
{
    at_handler->lock();

    at_handler->at_cmd_discard("#LWM2MSET", "=", "%d%d%d%d%d%d", 0, 3, 0, 9, 0, battery_level);

    return at_handler->unlock_return_error();
}

bool OneEdgeService::file_exists(char *target_file)
{
    at_handler->lock();

    at_handler->cmd_start_stop("#M2MLIST", "=/XML");
    at_handler->resp_start("#M2MLIST:");

    while (at_handler->info_resp()) {
        char m2mlist_entry[ME310_MAX_FULL_FILE_PATH_LENGTH];
        at_handler->read_string(m2mlist_entry, sizeof(m2mlist_entry));
        if (strstr(m2mlist_entry, target_file) != NULL) {
            at_handler->resp_stop();
            at_handler->unlock();
            return true;
        }
    }

    at_handler->resp_stop();
    at_handler->unlock();
    return false;
}

bool OneEdgeService::lwm2m_client_enable_temperature_object()
{
    // Check if the object description file already exists on the modem
    if (file_exists("object_3303.xml")) {
        tr_debug("'object_3303.xml' file found!");
        return true;
    }

    at_handler->lock();

    int write_size = 0;

    // Write the file to the modem
    at_handler->cmd_start_stop("#M2MWRITE", "=", "%s%d", "/XML/object_3303.xml", strlen(get_object_3303()));
    at_handler->resp_start(">>>", true);

    if (at_handler->get_last_error() != NSAPI_ERROR_OK) {
        tr_warn("Unable to send file");
        at_handler->unlock();
        return false;
    }

    write_size = at_handler->write_bytes((uint8_t *)get_object_3303(), strlen(get_object_3303()));
    if (write_size < strlen(get_object_3303())) {
        tr_warn("Unable to send full object_3303.xml file");
        at_handler->unlock();
        return false;
    }
    at_handler->resp_start("\r\nOK", true);
    at_handler->resp_stop();

    if (at_handler->get_last_error() != NSAPI_ERROR_OK) {
        tr_warn("Error sending object_3303.xml file");
        at_handler->unlock();
        return false;
    }

    tr_debug("object_3303.xml file sent");
    at_handler->unlock();
    return true;
}

bool OneEdgeService::lwm2m_client_create_temperature_object_instance(int instance)
{
    at_handler->lock();
    
    // Read the resource first to see if it already exists
    at_handler->at_cmd_discard("#LWM2MR", "=", "%d%d%d%d%d",
                0,          // Telit instance
                3303,       // Temperature object
                instance,   // Object instance
                5700,       // Current value resource ID
                0);         // Resource instance ID
    if (at_handler->get_last_error() == NSAPI_ERROR_OK) {
        // Resource already exists
        at_handler->unlock();
        return true;
    }

    at_handler->clear_error();
    at_handler->flush();
    at_handler->at_cmd_discard("#LWM2MNEWINST", "=", "%d%d%d", 0, 3303, instance);

    return at_handler->unlock_return_error() == NSAPI_ERROR_OK;
}

nsapi_error_t OneEdgeService::lwm2m_client_send_ack(int action)
{
    at_handler->lock();

    at_handler->at_cmd_discard("#LWM2MACK", "=", "%d", action);

    return at_handler->unlock_return_error();
}

void OneEdgeService::urc_lwm2m_tlt()
{
    char current_state[ONEEDGE_CLIENT_STATE_MAX_LENGTH];

    at_handler->lock();
    const ssize_t current_state_length = at_handler->read_string(current_state, ONEEDGE_CLIENT_STATE_MAX_LENGTH);
    at_handler->skip_param();
    at_handler->skip_param();
    const nsapi_error_t err = at_handler->unlock_return_error();

    tr_debug("Found LWM2M-TLT URC, new state: %s", current_state);

    if (err != NSAPI_ERROR_OK) {
        return;
    }
}

void OneEdgeService::urc_lwm2mring()
{
    char current_ring_state[ONEEDGE_CLIENT_STATE_MAX_LENGTH];

    at_handler->lock();
    const ssize_t current_state_length = at_handler->read_string(current_ring_state, ONEEDGE_CLIENT_STATE_MAX_LENGTH);
    const nsapi_error_t err = at_handler->unlock_return_error();

    tr_debug("Found #LWM2MRING URC, new state: %s", current_ring_state);

    if (err != NSAPI_ERROR_OK) {
        return;
    }
}

void OneEdgeService::urc_lwm2mend()
{
    at_handler->lock();
    const int end_result_code = at_handler->read_int();
    const nsapi_error_t err = at_handler->unlock_return_error();

    tr_debug("Found #LWM2MEND URC, end result code: %d", end_result_code);

    if (err != NSAPI_ERROR_OK) {
        return;
    }
}

void OneEdgeService::urc_lwm2minfo()
{
    char info_type[ONEEDGE_CLIENT_STATE_MAX_LENGTH];
    char info_event[ONEEDGE_CLIENT_STATE_MAX_LENGTH];

    at_handler->lock();
    const ssize_t info_type_length = at_handler->read_string(info_type, ONEEDGE_CLIENT_STATE_MAX_LENGTH);
    const ssize_t info_event_length = at_handler->read_string(info_event, ONEEDGE_CLIENT_STATE_MAX_LENGTH);
    const nsapi_error_t err = at_handler->unlock_return_error();

    tr_debug("Found #LWM2MINFO URC, info type: %s, info event: %s", info_type, info_event);

    if (err != NSAPI_ERROR_OK) {
        return;
    }
}