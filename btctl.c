/*
 *  Android Bluetooth Control tool
 *
 *  Copyright (C) 2013 João Paulo Rechi Vita
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hardware/bluetooth.h>
#include <hardware/bt_gatt.h>
#include <hardware/bt_gatt_client.h>
#include <hardware/hardware.h>

#include "util.h"

#define MAX_LINE_SIZE 64

/* Data that have to be acessable by the callbacks */
struct userdata {
    const bt_interface_t *btiface;
    uint8_t btiface_initialized;
    const btgatt_interface_t *gattiface;
    uint8_t gattiface_initialized;
    uint8_t quit;
    bt_state_t adapter_state; /* The adapter is always OFF in the beginning */
    bt_discovery_state_t discovery_state;
    uint8_t scan_state;
    bool client_registered;
    int client_if;
    bt_bdaddr_t remote_addr;
    int conn_id;
} u;

/* Arbitrary UUID used to identify this application with the GATT library. The
 * Android JAVA framework
 * (frameworks/base/core/java/android/bluetooth/BluetoothUuid.java) uses the
 * method randomUUID()
 */
static bt_uuid_t app_uuid = {
    .uu = { 0x1b, 0x1c, 0xb9, 0x2e, 0x0d, 0x2e, 0x4c, 0x45, \
            0xbb, 0xb9, 0xf4, 0x1b, 0x46, 0x39, 0x23, 0x36 }
};

/* Prints the command prompt */
static void cmd_prompt() {
    printf("> ");
    fflush(stdout);
}

/* Struct that defines a user command */
typedef struct cmd {
    const char *name;
    const char *description;
    void (*handler)(char *args);
} cmd_t;

/* Clean blanks until a non-blank is found */
static void line_skip_blanks(char **line) {
    while (**line == ' ')
        (*line)++;
}

/* Parses a sub-string out of a string */
static void line_get_str(char **line, char *str) {
  line_skip_blanks(line);

  while (**line != 0 && **line != ' ') {
        *str = **line;
        (*line)++;
        str++;
  }

  *str = 0;
}

static void cmd_quit(char *args) {
    u.quit = 1;
}

/* Called every time the adapter state changes */
static void adapter_state_change_cb(bt_state_t state) {

    u.adapter_state = state;
    printf("\nAdapter state changed: %i\n", state);

    if (state ==  BT_STATE_ON) {
       /* Register as a GATT client with the stack
        *
	* This has to be done here because it is the first available point we're
	* sure the GATT interface is initialized and ready to be used, since
	* there is callback for gattiface->init().
        */
        bt_status_t status = u.gattiface->client->register_client(&app_uuid);
        if (status != BT_STATUS_SUCCESS)
            printf("Failed to register as a GATT client, status: %d\n", status);
    } else
        /* when state goes to BT_STATE_ON prompt will be printed by
         * register_client_cb() */
        cmd_prompt();
}

/* Enables the Bluetooth adapter */
static void cmd_enable(char *args) {
    int status;

    if (u.adapter_state == BT_STATE_ON) {
        printf("Bluetooth is already enabled\n");
        return;
    }

    status = u.btiface->enable();
    if (status != BT_STATUS_SUCCESS)
        printf("Failed to enable Bluetooth\n");
}

/* Disables the Bluetooth adapter */
static void cmd_disable(char *args) {
    bt_status_t result;
    int status;

    if (u.adapter_state == BT_STATE_OFF) {
        printf("Bluetooth is already disabled\n");
        return;
    }

    result = u.gattiface->client->unregister_client(u.client_if);
    if (result != BT_STATUS_SUCCESS)
        printf("Failed to unregister client, error: %u\n", result);

    status = u.btiface->disable();
    if (status != BT_STATUS_SUCCESS)
        printf("Failed to disable Bluetooth\n");
}

static void device_found_cb(int num_properties, bt_property_t *properties) {

    printf("\nDevice found\n");

    while (num_properties--) {
        bt_property_t prop = properties[num_properties];

        switch (prop.type) {
            const char *addr;

            case BT_PROPERTY_BDNAME:
                printf("  name: %s\n", (const char *) prop.val);
                break;

            case BT_PROPERTY_BDADDR:
                addr = (const char *) prop.val;
                printf("  addr: %02X:%02X:%02X:%02X:%02X:%02X\n", addr[0],
                       addr[1], addr[2], addr[3], addr[4], addr[5]);
                break;

            case BT_PROPERTY_CLASS_OF_DEVICE:
                printf("  class: 0x%x\n", ((uint32_t *) prop.val)[0]);
                break;

            case BT_PROPERTY_TYPE_OF_DEVICE:
                switch ( ((bt_device_type_t *) prop.val)[0] ) {
                    case BT_DEVICE_DEVTYPE_BREDR:
                        printf("  type: BR/EDR only\n");
                        break;
                    case BT_DEVICE_DEVTYPE_BLE:
                        printf("  type: LE only\n");
                        break;
                    case BT_DEVICE_DEVTYPE_DUAL:
                        printf("  type: DUAL MODE\n");
                        break;
                }
                break;

            case BT_PROPERTY_REMOTE_FRIENDLY_NAME:
                printf("  alias: %s\n", (const char *) prop.val);
                break;

            case BT_PROPERTY_REMOTE_RSSI:
                printf("  rssi: %i\n", ((uint8_t *) prop.val)[0]);
                break;

            case BT_PROPERTY_REMOTE_VERSION_INFO:
                printf("  version info:\n");
                printf("    version: %d\n", ((bt_remote_version_t *) prop.val)->version);
                printf("    subversion: %d\n", ((bt_remote_version_t *) prop.val)->sub_ver);
                printf("    manufacturer: %d\n", ((bt_remote_version_t *) prop.val)->manufacturer);
                break;

            default:
                printf("  Unknown property type:%i len:%i val:%p\n", prop.type, prop.len, prop.val);
                break;
        }
    }

    cmd_prompt();
}

static void discovery_state_changed_cb(bt_discovery_state_t state) {
    u.discovery_state = state;
    printf("\nDiscovery state changed: %i\n", state);
    cmd_prompt();
}

static void cmd_discovery(char *args) {
    bt_status_t status;
    char arg[MAX_LINE_SIZE];

    line_get_str(&args, arg);

    if (arg[0] == 0 || strcmp(arg, "help") == 0) {
        printf("discovery -- Controls discovery of nearby devices\n");
        printf("Arguments:\n");
        printf("start   starts a new discovery session\n");
        printf("stop    interrupts an ongoing discovery session\n");

    } else if (strcmp(arg, "start") == 0) {

        if (u.adapter_state != BT_STATE_ON) {
            printf("Unable to start discovery: Adapter is down\n");
            return;
        }

        if (u.discovery_state == BT_DISCOVERY_STARTED) {
            printf("Discovery is already running\n");
            return;
        }

        status = u.btiface->start_discovery();
        if (status != BT_STATUS_SUCCESS)
            printf("Failed to start discovery\n");

    } else if (strcmp(arg, "stop") == 0) {

        if (u.discovery_state == BT_DISCOVERY_STOPPED) {
            printf("Unable to stop discovery: Discovery is not running\n");
            return;
        }

        status = u.btiface->cancel_discovery();
        if (status != BT_STATUS_SUCCESS)
            printf("Failed to stop discovery\n");

    } else
        printf("Invalid argument \"%s\"\n", arg);
}

static void scan_result_cb(bt_bdaddr_t *bda, int rssi, uint8_t *adv_data) {
    int i;

    printf("\nBLE device found\n");
    printf("  addr: %02X:%02X:%02X:%02X:%02X:%02X\n", bda->address[0],
           bda->address[1], bda->address[2], bda->address[3], bda->address[4],
           bda->address[5]);
    printf("  rssi: %d\n", rssi);
    /* TODO: parse the advertising data */
    printf("  advertising data:");
    for (i = 0; i < 31; i++)
        printf(" %02X", adv_data[i]);
    printf("\n");

    cmd_prompt();
}

static void cmd_scan(char *args) {
    bt_status_t status;
    char arg[MAX_LINE_SIZE];

    if (u.gattiface == NULL) {
        printf("Unable to start/stop BLE scan: GATT interface not available\n");
        return;
    }

    line_get_str(&args, arg);

    if (arg[0] == 0 || strcmp(arg, "help") == 0) {
        printf("scan -- Controls BLE scan of nearby devices\n");
        printf("Arguments:\n");
        printf("start   starts a new scan session\n");
        printf("stop    interrupts an ongoing scan session\n");

    } else if (strcmp(arg, "start") == 0) {

        if (u.adapter_state != BT_STATE_ON) {
            printf("Unable to start discovery: Adapter is down\n");
            return;
        }

        if (u.scan_state == 1) {
            printf("Scan is already running\n");
            return;
        }

        status = u.gattiface->client->scan(u.client_if, 1);
        if (status != BT_STATUS_SUCCESS) {
            printf("Failed to start discovery\n");
            return;
        }

        u.scan_state = 1;

    } else if (strcmp(arg, "stop") == 0) {

        if (u.scan_state == 0) {
            printf("Unable to stop scan: Scan is not running\n");
            return;
        }

        status = u.gattiface->client->scan(u.client_if, 0);
        if (status != BT_STATUS_SUCCESS) {
            printf("Failed to stop scan\n");
            return;
        }

        u.scan_state = 0;

    } else
        printf("Invalid argument \"%s\"\n", arg);
}

static void connect_cb(int conn_id, int status, int client_if,
                       bt_bdaddr_t *bda) {

    if (status != 0) {
        printf("Failed to connect to device %02X:%02X:%02X:%02X:%02X:%02X, "
                            "status: %i\n", bda->address[0], bda->address[1],
                            bda->address[2], bda->address[3], bda->address[4],
                            bda->address[5], status);
        return;
    }

    printf("Connected to device %02X:%02X:%02X:%02X:%02X:%02X, conn_id: %d, "
                            "client_if: %d\n", bda->address[0], bda->address[1],
                            bda->address[2], bda->address[3], bda->address[4],
                            bda->address[5], conn_id, client_if);
    u.conn_id = conn_id;
}

static void disconnect_cb(int conn_id, int status, int client_if,
                          bt_bdaddr_t *bda) {

    printf("Disconnected from device %02X:%02X:%02X:%02X:%02X:%02X, "
            "conn_id: %d, client_if: %d, status: %d\n", bda->address[0],
            bda->address[1], bda->address[2], bda->address[3], bda->address[4],
            bda->address[5], conn_id, client_if, status);

    u.conn_id = 0;
}

static void cmd_disconnect(char *args) {
    bt_status_t status;

    if (u.conn_id <= 0) {
        printf("Device not connected\n");
        return;
    }

    status = u.gattiface->client->disconnect(u.client_if, &u.remote_addr,
                                             u.conn_id);
    if (status != BT_STATUS_SUCCESS) {
        printf("Failed to disconnect, status: %d\n", status);
        return;
    }
}

void ssp_request_cb(bt_bdaddr_t *remote_bd_addr, bt_bdname_t *bd_name,
            uint32_t cod, bt_ssp_variant_t pairing_variant, uint32_t pass_key) {

    printf("Remote addr: %02X:%02X:%02X:%02X:%02X:%02X\n",
           remote_bd_addr->address[0], remote_bd_addr->address[1],
           remote_bd_addr->address[2], remote_bd_addr->address[3],
           remote_bd_addr->address[4], remote_bd_addr->address[5]);

    printf("Enter passkey on peer device: %d\n", pass_key);
}

static void cmd_connect(char *args) {
    bt_status_t status;
    char arg[MAX_LINE_SIZE];
    int ret;

    if (u.gattiface == NULL) {
        printf("Unable to BLE connect: GATT interface not available\n");
        return;
    }

    if (u.adapter_state != BT_STATE_ON) {
        printf("Unable to connect: Adapter is down\n");
        return;
    }

    if (u.client_registered == false) {
        printf("Unable to connect: We're not registered as GATT client\n");
        return;
    }

    line_get_str(&args, arg);

    ret = str2ba(arg, &u.remote_addr);
    if (ret != 0) {
        printf("Unable to connect: Invalid bluetooth address: %s\n", arg);
        return;
    }

    printf("Connecting to: %s\n", arg);

    status = u.gattiface->client->connect(u.client_if, &u.remote_addr, true);
    if (status != BT_STATUS_SUCCESS) {
        printf("Failed to connect, status: %d\n", status);
        return;
    }
}

static void bond_state_changed_cb(bt_status_t status, bt_bdaddr_t *bda,
                                  bt_bond_state_t state) {

    if (status != BT_STATUS_SUCCESS) {
        printf("Failed to change bond state, status: %d\n", status);
        return;
    }

    printf("Bond state changed for device %02X:%02X:%02X:%02X:%02X:%02X: ",
           bda->address[0], bda->address[1], bda->address[2], bda->address[3],
           bda->address[4], bda->address[5]);

    switch (state) {
        case BT_BOND_STATE_NONE:
            printf("BT_BOND_STATE_NONE\n");
            break;

        case BT_BOND_STATE_BONDING:
            printf("BT_BOND_STATE_BONDING\n");
            break;

        case BT_BOND_STATE_BONDED:
            printf("BT_BOND_STATE_BONDED\n");
            break;

        default:
            printf("Unknown (%d)\n", state);
            break;
    }
}

static void cmd_pair(char *args) {
    char arg[MAX_LINE_SIZE];
    unsigned arg_pos;
    bt_bdaddr_t addr;
    bt_status_t status;
    int ret;
    const char* valid_arguments[] = {
        "create",
        "cancel",
        "remove",
        NULL,
    };

    if (u.btiface == NULL) {
        printf("Unable to BLE pair: Bluetooth interface not available\n");
        return;
    }

    if (u.adapter_state != BT_STATE_ON) {
        printf("Unable to pair: Adapter is down\n");
        return;
    }

    line_get_str(&args, arg);

    if (arg[0] == 0 || strcmp(arg, "help") == 0) {
        printf("bond -- Controls BLE bond process\n");
        printf("Arguments:\n");
        printf("create <address>   start bond process to address\n");
        printf("cancel <address>   cancel bond process to address\n");
        printf("remove <address>   remove bond to address\n");
        return;
    }

    arg_pos = str_in_list(valid_arguments, arg);
    if (str_in_list(valid_arguments, arg) < 0) {

        printf("Invalid argument \"%s\"\n", arg);
        return;
    }

    line_get_str(&args, arg);
    ret = str2ba(arg, &addr);
    if (ret != 0) {
        printf("Invalid bluetooth address: %s\n", arg);
        return;
    }

    switch (arg_pos) {
        case 0:
           status = u.btiface->create_bond(&addr);
            if (status != BT_STATUS_SUCCESS) {
                printf("Failed to create bond, status: %d\n", status);
                return;
            }
            break;
        case 1:
           status = u.btiface->cancel_bond(&addr);
            if (status != BT_STATUS_SUCCESS) {
                printf("Failed to cancel bond, status: %d\n", status);
                return;
            }
            break;
        case 2:
           status = u.btiface->remove_bond(&addr);
            if (status != BT_STATUS_SUCCESS) {
                printf("Failed to remove bond, status: %d\n", status);
                return;
            }
            break;
    }
}

/* List of available user commands */
static const cmd_t cmd_list[] = {
    { "quit", "        Exits", cmd_quit },
    { "enable", "      Enables the Bluetooth adapter", cmd_enable },
    { "disable", "     Disables the Bluetooth adapter", cmd_disable },
    { "discovery", "   Controls discovery of nearby devices", cmd_discovery },
    { "scan", "        Controls BLE scan of nearby devices", cmd_scan },
    { "connect", "     Create a connection to a remote device", cmd_connect },
    { "pair", "        Pair with remote device", cmd_pair },
    { "disconnect", "  Disconnect from remote device", cmd_disconnect },
    { NULL, NULL, NULL }
};

/* Parses a command and calls the respective handler */
static void cmd_process(char *line) {
    char cmd[MAX_LINE_SIZE];
    int i;

    if (line[0] == 0)
        return;

    line_get_str(&line, cmd);

    if (strcmp(cmd, "help") == 0) {
        for (i = 0; cmd_list[i].name != NULL; i++)
            printf("%s %s\n", cmd_list[i].name, cmd_list[i].description);
        return;
    }

    for (i = 0; cmd_list[i].name != NULL; i++)
        if (strcmp(cmd, cmd_list[i].name) == 0) {
            cmd_list[i].handler(line);
            return;
        }

    printf("%s: unknown command, use 'help' for a list of available commands\n", cmd);
}

static void register_client_cb(int status, int client_if,
                               bt_uuid_t *app_uuid) {

    if (status != BT_STATUS_SUCCESS) {
        printf("Failed to register client, status: %d\n", status);
        return;
    }

    printf("Registered!, client_if: %d\n", client_if);

    u.client_if = client_if;
    u.client_registered = true;

    cmd_prompt();
}

/* GATT client callbacks */
static const btgatt_client_callbacks_t gattccbs = {
    register_client_cb, /* Called after client is registered */
    scan_result_cb, /* called every time an advertising report is seen */
    connect_cb, /* called every time a connection attempt finishes */
    disconnect_cb, /* called every time a connection attempt finishes */
    NULL, /* search_complete_callback */
    NULL, /* search_result_callback */
    NULL, /* get_characteristic_callback */
    NULL, /* get_descriptor_callback */
    NULL, /* get_included_service_callback */
    NULL, /* register_for_notification_callback */
    NULL, /* notify_callback */
    NULL, /* read_characteristic_callback */
    NULL, /* write_characteristic_callback */
    NULL, /* read_descriptor_callback */
    NULL, /* write_descriptor_callback */
    NULL, /* execute_write_callback */
    NULL  /* read_remote_rssi_callback */
};

/* GATT interface callbacks */
static const btgatt_callbacks_t gattcbs = {
    sizeof(btgatt_callbacks_t),
    &gattccbs,
    NULL  /* btgatt_server_callbacks_t */
};

/* This callback is used by the thread that handles Bluetooth interface (btif)
 * to send events for its users. At the moment there are two events defined:
 *
 *     ASSOCIATE_JVM: sinalizes the btif handler thread is ready;
 *     DISASSOCIATE_JVM: sinalizes the btif handler thread is about to exit.
 *
 * This events are used by the JNI to know when the btif handler thread should
 * be associated or dessociated with the JVM
 */
static void thread_event_cb(bt_cb_thread_evt event) {
    printf("\nBluetooth interface %s\n", event == ASSOCIATE_JVM ? "ready" : "finished");
    if (event == ASSOCIATE_JVM) {
        u.btiface_initialized = 1;

        u.gattiface = u.btiface->get_profile_interface(BT_PROFILE_GATT_ID);
        if (u.gattiface != NULL) {
            bt_status_t status = u.gattiface->init(&gattcbs);
            if (status != BT_STATUS_SUCCESS) {
                printf("Failed to initialize Bluetooth GATT interface, status: %d\n", status);
                u.gattiface = NULL;
            } else
                u.gattiface_initialized = 1;
        } else
            printf("Failed to get Bluetooth GATT Interface\n");

        cmd_prompt();

    } else
        u.btiface_initialized = 0;
}

/* Bluetooth interface callbacks */
static bt_callbacks_t btcbs = {
    sizeof(bt_callbacks_t),
    adapter_state_change_cb, /* Called every time the adapter state changes */
    NULL, /* adapter_properties_callback */
    NULL, /* remote_device_properties_callback */
    device_found_cb, /* Called for every device found */
    discovery_state_changed_cb, /* Called every time the discovery state changes */
    NULL, /* pin_request_callback */
    ssp_request_cb, /* ssp_request_callback */
    bond_state_changed_cb, /* bond_state_changed_callback */
    NULL, /* acl_state_changed_callback */
    thread_event_cb, /* Called when the JVM is associated / dissociated */
    NULL, /* dut_mode_recv_callback */
    NULL, /* le_test_mode_callback */
};

/* Initialize the Bluetooth stack */
static void bt_init() {
    int status;
    hw_module_t *module;
    hw_device_t *hwdev;
    bluetooth_device_t *btdev;

    u.btiface_initialized = 0;
    u.quit = 0;
    u.adapter_state = BT_STATE_OFF; /* The adapter is OFF in the beginning */
    u.conn_id = 0;

    /* Get the Bluetooth module from libhardware */
    status = hw_get_module(BT_STACK_MODULE_ID, (hw_module_t const**) &module);
    if (status < 0) {
        errno = status;
        err(1, "Failed to get the Bluetooth module");
    }
    printf("Bluetooth stack infomation:\n");
    printf("    id = %s\n", module->id);
    printf("    name = %s\n", module->name);
    printf("    author = %s\n", module->author);
    printf("    HAL API version = %d\n", module->hal_api_version);

    /* Get the Bluetooth hardware device */
    status = module->methods->open(module, BT_STACK_MODULE_ID, &hwdev);
    if (status < 0) {
        errno = status;
        err(2, "Failed to get the Bluetooth hardware device");
    }
    printf("Bluetooth device infomation:\n");
    printf("    API version = %d\n", hwdev->version);

    /* Get the Bluetooth interface */
    btdev = (bluetooth_device_t *) hwdev;
    u.btiface = btdev->get_bluetooth_interface();
    if (u.btiface == NULL)
        err(3, "Failed to get the Bluetooth interface");

    /* Init the Bluetooth interface, setting a callback for each operation */
    status = u.btiface->init(&btcbs);
    if (status != BT_STATUS_SUCCESS && status != BT_STATUS_DONE)
        err(4, "Failed to initialize the Bluetooth interface");
}

int main (int argc, char * argv[]) {
    char line[MAX_LINE_SIZE];

    printf("Android Bluetooth control tool version 0.1\n");

    bt_init();

    cmd_prompt();

    while (!u.quit && fgets(line, MAX_LINE_SIZE, stdin)) {
        /* remove linefeed */
        line[strlen(line)-1] = 0;

        cmd_process(line);

        if (!u.quit)
            cmd_prompt();
    }

    /* Disable adapter on exit */
    if (u.adapter_state == BT_STATE_ON)
        cmd_disable(NULL);

    /* Cleanup the Bluetooth interface */
    printf("Processing Bluetooth interface cleanup");
    u.btiface->cleanup();
    while (u.btiface_initialized)
        usleep(10000);

    printf("\n");
    return 0;
}
