/*------------------------------------------------------------------------------
This file is protected under copyright. If you want to use it,
please include this text, that is my only stipulation.  

Author: Evan Giese
------------------------------------------------------------------------------*/
#include "mib.h"
#include "port.h"
#include "file_delivery_app.h"
#include "tasks.h"

static int create_ssp_server_drivers(FTP *app) {

    switch (app->remote_entity.type_of_network)
    {
        case posix_connectionless:
            app->server_handle = ssp_thread_create(STACK_ALLOCATION, ssp_connectionless_server_task, app);
            break;
        case posix_connection:
            app->server_handle = ssp_thread_create(STACK_ALLOCATION, ssp_connection_server_task, app);
            break;
        case csp_connectionless:
            app->server_handle = ssp_thread_create(STACK_ALLOCATION, ssp_csp_connectionless_server_task, app);
            break;
        case csp_connection:
            app->server_handle = ssp_thread_create(STACK_ALLOCATION, ssp_csp_connection_server_task, app);
            break;
        case generic:
            app->server_handle = ssp_thread_create(STACK_ALLOCATION, ssp_generic_server_task, app);
            break;
        default:
            ssp_printf("server couldn't start, 'type of network' not recognized\n");
            break;
    }
    if (app->server_handle == NULL) {
        return -1;
    }
    return 0;

}

static int create_ssp_client_drivers(Client *client) {
    Remote_entity remote_entity = client->remote_entity;

    switch (remote_entity.type_of_network)
    {
        case posix_connectionless:
            client->client_handle = ssp_thread_create(STACK_ALLOCATION, ssp_connectionless_client_task, client);
            break;
        case posix_connection:
            client->client_handle = ssp_thread_create(STACK_ALLOCATION, ssp_connection_client_task, client);
            break;
        case csp_connectionless:
            client->client_handle = ssp_thread_create(STACK_ALLOCATION, ssp_csp_connectionless_client_task, client);
            break;
        case csp_connection:
            client->client_handle = ssp_thread_create(STACK_ALLOCATION, ssp_csp_connection_client_task, client);
            break;
        case generic:
            client->client_handle = ssp_thread_create(STACK_ALLOCATION, ssp_generic_client_task, client);
            break;
        default:
            ssp_printf("client couldn't start, 'type of network' not recognized\n");
            break;
    }
    if (client->client_handle == NULL) {
        return -1;
    }
    return 0;
}



FTP *init_ftp(uint32_t my_cfdp_address) {

    int error = ssp_mkdir("incomplete_requests");
    if (error < 0) {
        ssp_error("couldn't make directory incomplete_requests \n");
        return NULL;
    }
    error = ssp_mkdir("mib");
    if (error < 0) {
        ssp_error("couldn't make directory mib\n");
        return NULL;
    }

    int fd = ssp_open("mib/peer_0.json", SSP_O_CREAT | SSP_O_RDWR);
    if (fd < 0) {
        ssp_error("couldn't create default peer_0.json file\n");
        return NULL;
    }

    const char *peer_file = "{\n\
    \"cfdp_id\": 7,\n\
    \"UT_address\" : 0,\n\
    \"UT_port\" : 1,\n\
    \"type_of_network\" : 3,\n\
    \"default_transmission_mode\" : 1,\n\
    \"MTU\" : 250,\n\
    \"one_way_light_time\" : 123,\n\
    \"total_round_trip_allowance\" : 123,\n\
    \"async_NAK_interval\" : 123,\n\
    \"async_keep_alive_interval\" : 123,\n\
    \"async_report_interval\" : 123,\n\
    \"immediate_nak_mode_enabled\" : 123,\n\
    \"prompt_transmission_interval\" : 123,\n\
    \"disposition_of_incomplete\" : 123,\n\
    \"CRC_required\" : 0,\n\
    \"keep_alive_discrepancy_limit\" : 8,\n\
    \"positive_ack_timer_expiration_limit\" : 123,\n\
    \"nak_timer_expiration_limit\" : 123,\n\
    \"transaction_inactivity_limit\" : 123\n\
}";
    
    error = ssp_write(fd, peer_file, strnlen(peer_file, 1000));
    if (error < 0) {
        ssp_error("couldn't write default file\n");
    }

    
    Remote_entity remote_entity;
    error = get_remote_entity_from_json(&remote_entity, my_cfdp_address);
    if (error == -1) {
        ssp_error("can't get configuration data, can't start server.\n");
        return NULL;
    }
    
    FTP *app = ssp_alloc(sizeof(FTP), 1);
    if (app == NULL) 
        return NULL;

    app->packet_len = remote_entity.mtu;
    app->buff = ssp_alloc(1, app->packet_len);
    if (app->buff == NULL) {
        ssp_free(app);
        return NULL;
    }

    app->my_cfdp_id = my_cfdp_address;
    app->close = false;
    app->remote_entity = remote_entity;

    app->active_clients = linked_list();
    if (app->active_clients == NULL) {
        ssp_free(app->buff);
        ssp_free(app);
        return NULL;
    }

    app->request_list = linked_list();
    if (app->request_list == NULL){
        ssp_free(app->buff);
        ssp_free(app);
        app->active_clients->freeOnlyList(app->active_clients);
        return NULL;
    }

    app->current_request = NULL;
    create_ssp_server_drivers(app);
    return app;
}



Client *ssp_client(uint32_t cfdp_id, FTP *app) {

    Remote_entity remote_entity;
    int error = get_remote_entity_from_json(&remote_entity, cfdp_id);
    if (error < 0) {
        ssp_error("couldn't get client remote_entity from mib\n");
        return NULL;
    }
    
    Client *client = ssp_alloc(sizeof(Client), 1);
    if (client == NULL)
        return NULL;

    client->request_list = linked_list();
    if (client->request_list == NULL) {
        ssp_free(client);
        return NULL;
    }

    client->packet_len = remote_entity.mtu;
    client->buff = ssp_alloc(1, remote_entity.mtu);
    if (client->buff == NULL){
        ssp_free(client);
        client->request_list->freeOnlyList(client->request_list);
        return NULL;
    }

    client->remote_entity = remote_entity;
    get_header_from_mib(&client->pdu_header, remote_entity, app->my_cfdp_id);
    
    client->current_request = NULL;
    client->app = app;

    error = create_ssp_client_drivers(client);
    if (error < 0) {
        ssp_free(client);
        ssp_free(client->buff);
        client->request_list->freeOnlyList(client->request_list);        
        return NULL;
    }
    return client;
}
