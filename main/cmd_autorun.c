/* cmd_autorun.c: implements `autorun_*` console commands
   2020/12/30 Written by Durval Menezes [https://github.com/DurvalMenezes]

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_err.h"

#include "esp_console.h"
#include "argtable3/argtable3.h"

#include "nvs.h"

#include "cmd_decl.h"
#include "iperf.h"

static const char current_namespace[16] = "autorun";

static const char *TAG = "cmd_autorun";

static struct {
    struct arg_str *cmdlist;
    struct arg_end *end;
} set_args;

static struct {
    struct arg_int *delay;
    struct arg_end *end;
} delay_args;

static struct {
    struct arg_str *taskname;
    struct arg_end *end;
} wait_args;

static esp_err_t fn_autorun_cmd_get(int argc, char **argv)
{
	esp_err_t err;
    nvs_handle nvs;
	char content[1024];
	size_t max_length = sizeof(content);

	//arg_parse() etc not necessary as get needs no args

    err = nvs_open(current_namespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
	
    err = nvs_get_str(nvs, "cmdlist", content, &max_length);
    if (err == ESP_OK) {
		ESP_LOGI(TAG, "fn_autorun_cmd_get(): Current autorun command-list is: [%s]", content);
	} else { 
		if (err == ESP_ERR_NVS_NOT_FOUND) {
			ESP_LOGI(TAG, "fn_autorun_cmd_get(): No command-list is configured.");
		} else {
			ESP_LOGE(TAG, "fn_autorun_cmd_get(): Unexpected error %d on nvs_get_str()", err);
        	return err;
		}
	}

    nvs_close(nvs);
    return ESP_OK;
}

static esp_err_t fn_autorun_cmd_set(int argc, char **argv)
{
    esp_err_t err;
    nvs_handle nvs;

	int nerrors = arg_parse(argc, argv, (void **) &set_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, set_args.end, argv[0]);
        return 1;
    }

    err = nvs_open(current_namespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_str(nvs, "cmdlist", set_args.cmdlist->sval[0]);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "fn_autorun_cmd_set(): Autorun command-list has been successfully set to [%s].", set_args.cmdlist->sval[0]);

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        return err;
    }

    nvs_close(nvs);
    return ESP_OK;
}

static esp_err_t fn_autorun_cmd_erase(int argc, char **argv)
{
	esp_err_t err;
    nvs_handle nvs;

	//arg_parse() etc not necessary as erase needs no args

    err = nvs_open(current_namespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_erase_key(nvs, "cmdlist");
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "fn_autorun_cmd_erase(): Autorun command-list has been successfully erased.");

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        return err;
    }

    nvs_close(nvs);
    return ESP_OK;
}

// this is to be called from the main program loop before the command-execute loop, to get the autorun command-list
char* fn_autorun_get(void)
{
	esp_err_t err;
    nvs_handle nvs;
	static char content[1024];
	size_t max_length = sizeof(content);

    err = nvs_open(current_namespace, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
		ESP_LOGE(TAG, "fn_autorun_get(): nvs_open() unexpected return code '0x%x'", err);
        return NULL;
    }
	
    err = nvs_get_str(nvs, "cmdlist", content, &max_length);
	if (err != ESP_OK) {
		if (err == ESP_ERR_NVS_NOT_FOUND) {
			return NULL;
		} else {
			ESP_LOGE(TAG, "fn_autorun_get(): nvs_get_str() unexpected return code '0x%x'", err);
			return NULL;
		}
	}

    nvs_close(nvs);
    return content;
}

static int fn_autorun_cmd_delay(int argc, char **argv)
{    
	int nerrors = arg_parse(argc, argv, (void **) &delay_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, delay_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG, "fn_autorun_cmd_delay(): delaying for %d milliseconds...", delay_args.delay->ival[0]);

    vTaskDelay(delay_args.delay->ival[0] / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "fn_autorun_cmd_delay(): done");

    return 0;
}

// LOCAL implementation of xTaskGetHandle(), as it's not currently enabled in the IDF; see https://github.com/espressif/esp-idf/issues/5519
static TaskHandle_t LOCAL_xTaskGetHandle(const char *taskname)
{
    //most of what follows was taken/adapted from the example at ...
    //... https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html#_CPPv420uxTaskGetSystemStatePC12TaskStatus_tK11UBaseType_tPC8uint32_t
    TaskStatus_t *pxTaskStatusArray;
    volatile UBaseType_t uxArraySize, ix;
    volatile TaskHandle_t th;

    // Take a snapshot of the number of tasks in case it changes while this
    // function is executing.
    uxArraySize = uxTaskGetNumberOfTasks();

    // Allocate a TaskStatus_t structure for each task.  An array could be
    // allocated statically at compile time.
    if ((pxTaskStatusArray = pvPortMalloc( uxArraySize * sizeof( TaskStatus_t ) )) == NULL) {
        ESP_LOGE(TAG, "LOCAL_xTaskGetHandle(): can't allocate pxTaskStatusArray, pvPortMalloc() returned NULL");
        return NULL;
    }

    // Get info on all tasks in the system
    uxArraySize = uxTaskGetSystemState( pxTaskStatusArray, uxArraySize, NULL );

    th = NULL;
    for (ix=0; ix<uxArraySize; ++ix) {
        if (strcmp(pxTaskStatusArray[ix].pcTaskName, taskname) == 0) {
           ESP_LOGI(TAG, "LOCAL_xTaskGetHandle(): found task '%s'", taskname);
           th = pxTaskStatusArray[ix].xHandle;
           break;
        }
    }

    vPortFree( pxTaskStatusArray );
    return th;
}

// LOCAL implementation of eTaskState(), as it's apparently not implemented either
static eTaskState LOCAL_eTaskState(TaskHandle_t th)
{
    //most of what follows was taken/adapted from the example at ...
    //... https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html#_CPPv412vTaskGetInfo12TaskHandle_tP12TaskStatus_t10BaseType_t10eTaskState
    TaskStatus_t pxTaskStatus;

    // Get info on task indicated by th
    vTaskGetInfo(th, &pxTaskStatus, pdFALSE, eInvalid);

    return pxTaskStatus.eCurrentState;
}

static esp_err_t fn_autorun_cmd_wait(int argc, char **argv)
{
    TaskHandle_t th; 
    eTaskState ts;

	int nerrors = arg_parse(argc, argv, (void **) &wait_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, wait_args.end, argv[0]);
        return 1;
    }

    ESP_LOGI(TAG, "fn_autorun_cmd_wait(): trying to get handle for task '%s'.", wait_args.taskname->sval[0]);

    if ((th = LOCAL_xTaskGetHandle(wait_args.taskname->sval[0])) == NULL) {
        ESP_LOGE(TAG, "fn_autorun_cmd_wait(): task '%s' not found", wait_args.taskname->sval[0]);
        return 2;
    }

    ESP_LOGI(TAG, "fn_autorun_cmd_wait(): task handle obtained, waiting for task to finish");

    while ( (ts = LOCAL_eTaskState(th)) == eRunning || (ts == eReady) || (ts == eBlocked) || (ts == eSuspended) )
       vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "fn_autorun_cmd_wait(): task '%s' has finished, continuing", wait_args.taskname->sval[0]);

    return ESP_OK;
}

void register_autorun()
{
    /*** set up and register each of the console commands processed by this module ***/

    //Command: autorun_get
    const esp_console_cmd_t autorun_get_cmd = {
        .command = "autorun_get",
        .help = "get the current autorun setting",
        .hint = NULL,
        .func = &fn_autorun_cmd_get,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&autorun_get_cmd) );

    //Command: autorun_set
    set_args.cmdlist = arg_str1(NULL, NULL, "<cmdlist>", "Command-list to autorun after boot: list of commands + arguments, exactly as\n" 
				                           	"typed, separated by semicollons");
    set_args.end = arg_end(2);
    const esp_console_cmd_t autorun_set_cmd = {
        .command = "autorun_set",
        .help = "configure a list of commands to run automatically after each boot",
        .hint = NULL,
        .func = &fn_autorun_cmd_set,
        .argtable = &set_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&autorun_set_cmd) );

    //Command: autorun_erase
    const esp_console_cmd_t autorun_erase_cmd = {
        .command = "autorun_erase",
        .help = "autorun erase command",
        .hint = NULL,
        .func = &fn_autorun_cmd_erase,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&autorun_erase_cmd) );

    //Command: autorun_delay
    delay_args.delay = arg_int0(NULL, NULL, "<milliseconds>", "number of milliseconds to delay");
    delay_args.end = arg_end(2);
    const esp_console_cmd_t autorun_delay_cmd = {
        .command = "autorun_delay",
        .help = "delay execution for a number of milliseconds",
        .hint = NULL,
        .func = &fn_autorun_cmd_delay,
        .argtable = &delay_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&autorun_delay_cmd) );

    //Command: autorun_wait
    wait_args.taskname = arg_str1(NULL, NULL, "<taskname>", "Name of the task to wait for");
    wait_args.end = arg_end(2);
    const esp_console_cmd_t autorun_wait_cmd = {
        .command = "autorun_wait",
        .help = "wait for a task to finish",
        .hint = NULL,
        .func = &fn_autorun_cmd_wait,
        .argtable = &wait_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&autorun_wait_cmd) );
}

//Eof cmd_autorun.c
