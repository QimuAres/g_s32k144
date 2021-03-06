#include "rtos.h"
#include "clockMan1.h"
#include "pin_mux.h"
#include "string.h"
#include "lpit_lld.h"
#include "freemaster.h"
#include "math.h"
#include "adConv1.h"
#include "pdb1.h"
#include "adc_lld.h"
#include "rtc_lld.h"
#include "lpuart_lld.h"
#include "wdg_lld.h"
#include "lptmr_lld.h"
#include "power_lld.h"
#include "gps_lld.h"
#include "printf.h"
#include "can_lld.h"

#define LED_TEST_MODE 0
#define FREERTOS_QUEUE_TEST_MODE 0

/* variables used for FreeRTOS monitoring */
uint32_t freertos_counter_1000ms = 0U;
uint32_t freertos_counter_1ms = 0U;
uint32_t freertos_counter_tick = 0U;
uint16_t lptmr_current_value_us;
uint16_t freertos_counter_1000ms_time_cost;
TaskHandle_t freertos_handle_uart_rx;
TaskHandle_t freertos_handle_1ms;
TaskHandle_t freertos_handle_1000ms;
TaskHandle_t freertos_handle_100ms;
TaskHandle_t freertos_handle_powermode;

/* variables used for test */
double value_sin_x;
double value_sin_y;
status_t power_mode_init_ret_val;
const char rmc_msg_test[] = "$GPRMC,021618.000,A,3150.7827,N,11711.8695,E,0.14,181.50,030119,,,A*76";

#if FREERTOS_QUEUE_TEST_MODE
QueueHandle_t freertos_queue_test = NULL;
#endif

void board_init(void)
{
    /* Initialize and configure clocks
     *  -   Setup system clocks, dividers
     *  -   see clock manager component for more details
     */
    CLOCK_SYS_Init(g_clockManConfigsArr, CLOCK_MANAGER_CONFIG_CNT,
                   g_clockManCallbacksArr, CLOCK_MANAGER_CALLBACK_CNT);
    CLOCK_SYS_UpdateConfiguration(0U, CLOCK_MANAGER_POLICY_AGREEMENT);
    PINS_DRV_Init(NUM_OF_CONFIGURED_PINS, g_pin_mux_InitConfigArr);
    PINS_DRV_SetPins(PTD, (1 << 0) | (1 << 15) | (1 << 16));
    EDMA_DRV_Init(&dmaController1_State, &dmaController1_InitConfig0,
                  edmaChnStateArray, edmaChnConfigArray, EDMA_CONFIGURED_CHANNELS_COUNT);
    lpuart_lld_init();
#if FMSTR_DISABLE
#else
    INT_SYS_InstallHandler(LPUART1_RxTx_IRQn, FMSTR_Isr, NULL);
    FMSTR_Init();
#endif
    adc_lld_init();
    rtc_lld_init();
    lpit_lld_init();
    wdg_lld_init();
    lptmr_lld_init();
    power_lld_init();
    SystemInit();
    power_mode_init_ret_val = POWER_SYS_SetMode(HSRUN, POWER_MANAGER_POLICY_AGREEMENT);
}

void rtos_start(void)
{
    UBaseType_t priority = 0U;
    /* Start the two tasks as described in the comments at the top of this
       file. */
#if FREERTOS_QUEUE_TEST_MODE
    freertos_queue_test = xQueueCreate(10, sizeof(unsigned long));
#endif

    xTaskCreate(freertos_task_uart_rx, "uart rx", configMINIMAL_STACK_SIZE, NULL, ++priority, &freertos_handle_uart_rx);
    xTaskCreate(freertos_task_1000ms, "1000ms", 2 * configMINIMAL_STACK_SIZE, NULL, ++priority, &freertos_handle_1000ms);
    xTaskCreate(freertos_task_100ms, "100ms", 1 * configMINIMAL_STACK_SIZE, NULL, ++priority, &freertos_handle_100ms);
    /* xTaskCreate(freertos_task_power_mode_test, "power-mode", 2 * configMINIMAL_STACK_SIZE, NULL, ++priority, &freertos_handle_powermode); */
    xTaskCreate(freertos_task_1ms, "1ms", configMINIMAL_STACK_SIZE, NULL, ++priority, &freertos_handle_1ms);
#if FREERTOS_QUEUE_TEST_MODE
    xTaskCreate(freertos_task_trigger_by_queue, "queue", configMINIMAL_STACK_SIZE, NULL, ++priority, NULL);
#endif
    /* Start the tasks and timer running. */
    vTaskStartScheduler();

    /* If all is well, the scheduler will now be running, and the following line
       will never be reached.  If the following line does execute, then there was
       insufficient FreeRTOS heap memory available for the idle and/or timer tasks
       to be created.  See the memory management section on the FreeRTOS web site
       for more details. */
    for (;;)
    {
        /* no code here */
    }
}

void freertos_task_100ms(void *pvParameters)
{
    (void)pvParameters;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(100UL));
        can_lld_step();
    }
}

void freertos_task_power_mode_test(void *pvParameters)
{
    uint32_t power_mode_counter = 0U;
    status_t ret_val;
    uint32_t core_frequency;

    (void)pvParameters;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(1000UL));
        power_mode_counter++;
        printf("power mode task running: %d\n", power_mode_counter);

        if (lpuart_lld_data_received_flg == 1U)
        {
            switch (lpuart_lld_rx_data[0])
            {
            case '1':
                printf("going to HRUN mode.\n");
                ret_val = POWER_SYS_SetMode(HSRUN, POWER_MANAGER_POLICY_AGREEMENT);
                if (STATUS_SUCCESS == ret_val)
                {
                    printf("now CPU is in HRUM mode.\n");
                    (void)CLOCK_SYS_GetFreq(CORE_CLOCK, &core_frequency);
                    printf("core frequency is: %d\n", core_frequency);
                }
                else
                {
                    printf("failed when change to HRUN mode.\n");
                }
                break;
            case '2':
                printf("going to RUN mode.\n");
                ret_val = POWER_SYS_SetMode(RUN, POWER_MANAGER_POLICY_AGREEMENT);
                if (ret_val == STATUS_SUCCESS)
                {
                    printf("now CPU is in RUN mode.\n");
                    (void)CLOCK_SYS_GetFreq(CORE_CLOCK, &core_frequency);
                    printf("core frequency is: %d\n", core_frequency);
                }
                else
                {
                    printf("failed when change to RUN mode.\n");
                }

                break;
            case '3':
                printf("going to VLPR mode.\n");
                ret_val = POWER_SYS_SetMode(VLPR, POWER_MANAGER_POLICY_AGREEMENT);
                if (ret_val == STATUS_SUCCESS)
                {
                    printf("now CPU is in VLPR mode.\n");
                    (void)CLOCK_SYS_GetFreq(CORE_CLOCK, &core_frequency);
                    printf("core frequency is: %d\n", core_frequency);
                }
                else
                {
                    printf("failed when change to VLPR mode.\n");
                }

                break;
            case '4':
                printf("going to STOP1 mode.\n");
                ret_val = POWER_SYS_SetMode(STOP1, POWER_MANAGER_POLICY_AGREEMENT);
                if (ret_val == STATUS_SUCCESS)
                {
                    printf("now CPU is in STOP1 mode.\n");
                    (void)CLOCK_SYS_GetFreq(CORE_CLOCK, &core_frequency);
                    printf("core frequency is: %d\n", core_frequency);
                }
                else
                {
                    printf("failed when change to STOP1 mode.\n");
                }

                break;
            case '5':
                printf("going to STOP2 mode.\n");
                ret_val = POWER_SYS_SetMode(STOP2, POWER_MANAGER_POLICY_AGREEMENT);
                if (ret_val == STATUS_SUCCESS)
                {
                    printf("now CPU is in STOP2 mode.\n");
                    (void)CLOCK_SYS_GetFreq(CORE_CLOCK, &core_frequency);
                    printf("core frequency is: %d\n", core_frequency);
                }
                else
                {
                    printf("failed when change to STOP2 mode.\n");
                }

                break;
            case '6':
                printf("going to VLPS mode.\n");
                ret_val = POWER_SYS_SetMode(VLPS, POWER_MANAGER_POLICY_AGREEMENT);
                if (ret_val == STATUS_SUCCESS)
                {
                    printf("now CPU is in VLPS mode.\n");
                    (void)CLOCK_SYS_GetFreq(CORE_CLOCK, &core_frequency);
                    printf("core frequency is: %d\n", core_frequency);
                }
                else
                {
                    printf("failed when change to VLPS mode.\n");
                }

                break;
            default:
                break;
            }
            lpuart_lld_data_received_flg = 0U;
        }
    }
}

void freertos_task_1000ms(void *pvParameters)
{
    TickType_t last_wake_time = 0U;
    const TickType_t delay_counter_1000ms = pdMS_TO_TICKS(1000UL);
    char test_str[] = "hello world\n";
    uint8_t tx_buf[20];
    uint32_t print_indicating_counter = 0U;
#if FREERTOS_QUEUE_TEST_MODE
    uint32_t counter_sent_by_queue = 0U;
    uint8_t i = 0U;
#endif
    enum minmea_sentence_id gps_msg_type;
    struct minmea_sentence_rmc gps_rmc_msg;

    (void)pvParameters;

    memcpy(tx_buf, test_str, sizeof(test_str));

    last_wake_time = xTaskGetTickCount();

    while (1)
    {
        lptmr_current_value_us = LPTMR_DRV_GetCounterValueByCount(INST_LPTMR1);
        freertos_counter_1000ms++;
        wdg_lld_feed_dog();
        printf("running time: %ds\n", freertos_counter_1000ms);
#if LED_TEST_MODE
        /* test code for LED blink */
        PINS_DRV_TogglePins(PTD, 1 << 0);
        PINS_DRV_TogglePins(PTD, 1 << 15);
        PINS_DRV_TogglePins(PTD, 1 << 16);
#endif
#if FREERTOS_QUEUE_TEST_MODE
        for (i = 0U; i < 9U; i++)
        {
            xQueueSend(freertos_queue_test, &counter_sent_by_queue, 0);
            counter_sent_by_queue++;
        }
#endif

        switch (print_indicating_counter)
        {
        case 1U:
            printf("%d. test for ADC:\n", print_indicating_counter);
            adc_lld_step();
            break;
        case 2U:
            printf("%d. test for RTC:\n", print_indicating_counter);
            rtc_lld_step();
            break;
        case 3U:
            printf("%d. test for 1ms task:\n", print_indicating_counter);
            printf("1ms counter is %d, %d times of 1000ms counter.\n",
                   freertos_counter_1ms, (freertos_counter_1ms / freertos_counter_1000ms));
            break;
        case 4U:
            if (freertos_counter_1ms != 0U)
            {
                printf("%d. test for FreeRTOS tick hook.\n", print_indicating_counter);
                printf("tick number is %d times of 1000ms counter.\n", freertos_counter_tick / freertos_counter_1000ms);
            }
            else
            {
                /* avoid divider is 0. */
            }
            break;
        case 5U:
            printf("%d. do some test for FreeRTOS.\n", print_indicating_counter);
            printf("priority of UART RX task: %d\n", uxTaskPriorityGet(freertos_handle_uart_rx));
            printf("priority of 1ms task: %d\n", uxTaskPriorityGet(freertos_handle_1ms));
            printf("priority of 1000ms task: %d\n", uxTaskPriorityGet(freertos_handle_1000ms));
            printf("free heap memory: %d bytes.\n", xPortGetFreeHeapSize());
            break;
        case 6U:
            printf("%d. do some test for lpTmr.\n", print_indicating_counter);
            lptmr_current_value_us = LPTMR_DRV_GetCounterValueByCount(INST_LPTMR1);
            printf("1000ms time cost is about: %dus\n", freertos_counter_1000ms_time_cost);
            if (LPTMR_DRV_GetCompareFlag(INST_LPTMR1))
            {
                LPTMR_DRV_ClearCompareFlag(INST_LPTMR1);
            }
            else
            {
                /* no code */
            }
            break;
        case 7U:
            printf("%d. test for GPS parese function.\n", print_indicating_counter);
            gps_msg_type = minmea_sentence_id(rmc_msg_test, false);
            gps_lld_display_msg_type(gps_msg_type);
            minmea_parse_rmc(&gps_rmc_msg, rmc_msg_test);
            printf("parse result of RMC message:\n");
            printf("    1) course is %f\n", (float)gps_rmc_msg.course.value / (float)gps_rmc_msg.course.scale);
            printf("    2) date and time is %02d-%02d-%02d %02d:%02d:%02d\n",
                   gps_rmc_msg.date.year, gps_rmc_msg.date.month, gps_rmc_msg.date.day,
                   gps_rmc_msg.time.hours, gps_rmc_msg.time.minutes, gps_rmc_msg.time.seconds);
            printf("    3) longitude is %f\n", (float)gps_rmc_msg.longitude.value / (float)gps_rmc_msg.longitude.scale);
            printf("    4) latitude is %f\n", (float)gps_rmc_msg.latitude.value / (float)gps_rmc_msg.latitude.scale);
            printf("    5) speed is %f\n", (float)gps_rmc_msg.speed.value / (float)gps_rmc_msg.speed.scale);
            break;
        default:
            print_indicating_counter = 0U;
            printf("%d-----new test loop started-----\n", print_indicating_counter);
            break;
        }

        if (lptmr_current_value_us < LPTMR_DRV_GetCounterValueByCount(INST_LPTMR1))
        {
            freertos_counter_1000ms_time_cost = LPTMR_DRV_GetCounterValueByCount(INST_LPTMR1) - lptmr_current_value_us;
        }

        print_indicating_counter++;
        vTaskDelayUntil(&last_wake_time, delay_counter_1000ms);
        SBC_FeedWatchdog();
    }
}

void freertos_task_1ms(void *pvParameters)
{
    const TickType_t delay_tick_1ms = pdMS_TO_TICKS(1UL);
    TickType_t last_wake_time = xTaskGetTickCount();

    (void)pvParameters;

    for (;;)
    {
        freertos_counter_1ms++;
        vTaskDelayUntil(&last_wake_time, delay_tick_1ms);
    }
}

#if FREERTOS_QUEUE_TEST_MODE
void freertos_task_trigger_by_queue(void *pvParameters)
{
    uint32_t received_data;
    uint8_t data[] = "deadbeaf\n";

    (void)pvParameters;

    while (1)
    {
        xQueueReceive(freertos_queue_test, &received_data, portMAX_DELAY);

        LPUART_DRV_SendDataBlocking(INST_LPUART1, &data[received_data % 9], 1, 100);
    }
}
#endif

void vApplicationIdleHook(void)
{
#if FMSTR_DISABLE
#else
    static FMSTR_APPCMD_CODE cmd;
    static FMSTR_APPCMD_PDATA cmdDataP;
    static FMSTR_SIZE cmdSize;

    value_sin_x += 0.0001;
    value_sin_y = sin(value_sin_x);

    /* Process FreeMASTER application commands */
    cmd = FMSTR_GetAppCmd();
    if (cmd != FMSTR_APPCMDRESULT_NOCMD)
    {
        cmdDataP = FMSTR_GetAppCmdData(&cmdSize);
        switch (cmd)
        {
        case 0:
            /* Acknowledge the command */
            FMSTR_AppCmdAck(0);
            break;
        case 1:
            /* Acknowledge the command */
            FMSTR_AppCmdAck(0);
            break;
        case 2:
            /* Acknowledge the command */
            FMSTR_AppCmdAck(0);
            break;
        case 3:
            /* Acknowledge the command */
            FMSTR_AppCmdAck(0);
            break;
        default:
            /* Acknowledge the command with failure */
            FMSTR_AppCmdAck(1);
            break;
        }
    }

    /* Handle the protocol decoding and execution */
    FMSTR_Poll();

    (void)cmdDataP;
#endif
}

void vApplicationTickHook(void)
{
    freertos_counter_tick++;
}

void vApplicationDaemonTaskStartupHook(void)
{
    printf("FreeRTOS daemon task started.\n");
    if (power_mode_init_ret_val != STATUS_SUCCESS)
    {
        printf("failed to change RUN mode.\n");
    }
    can_lld_init();
}
