#include <stdio.h>
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "rm_module.h"
#include "usbd_cdc_if.h"
#include "usbd_cdc.h"
#include "trans_task.h"
#include "gimbal_task.h"
#include "rc_dbus.h"

#define HEART_BEAT 500 // ms

/* ==================== USB接收：队列传输（新增）==================== */
#define USB_RX_MSG_LEN          512
#define USB_RX_MSG_COUNT        8

#define USB_TX_MSG_COUNT        8
#define USB_TX_PAYLOAD_MAX      512
#define USB_TX_FRAME_MAX        (2U + 2U + USB_TX_PAYLOAD_MAX + 2U)  // 帧头(2) + 长度(2) + 数据 + CRC16(2)

typedef struct
{
    uint16_t len;
    uint8_t data[USB_RX_MSG_LEN];
} usb_rx_msg_t;

typedef struct
{
    uint16_t len;
    uint8_t data[USB_TX_FRAME_MAX];
} usb_tx_msg_t;

static QueueHandle_t usb_rx_queue = NULL;
static usb_rx_msg_t usb_rx_msg_pool[USB_RX_MSG_COUNT];
static uint8_t usb_rx_msg_idx = 0;
static void process_usb_bytes(uint8_t* Buf, uint16_t Len);

static QueueHandle_t usb_tx_queue = NULL;
static QueueHandle_t usb_tx_free_queue = NULL;
static usb_tx_msg_t usb_tx_msg_pool[USB_TX_MSG_COUNT];
static usb_tx_msg_t *usb_tx_inflight = NULL;
static void usb_tx_pump(void);

extern USBD_HandleTypeDef hUsbDeviceHS;

/* ==================== 线程间通信相关 ==================== */
// 发布
MCN_DECLARE(transmission_fdb_topic);
static struct trans_fdb_msg trans_fdb_data;

// 订阅
MCN_DECLARE(ins_topic);
static McnNode_t ins_topic_node;
static struct ins_msg ins;

MCN_DECLARE(chassis_cmd);
static McnNode_t chassis_cmd_node;
static struct chassis_cmd_msg chass_cmd;

MCN_DECLARE(gimbal_cmd);
static McnNode_t gimbal_cmd_node;
static struct gimbal_cmd_msg gimbal_cmd;

MCN_DECLARE(gimbal_fdb_topic);
static McnNode_t gimbal_fdb_node;
static struct gimbal_fdb_msg gimbal_fdb;

MCN_DECLARE(gimbal_ins_topic);
static McnNode_t gimbal_ins_node;
static struct dm_imu_t gim_ins;

// 内部函数声明
static void trans_pub_push(void);
static void trans_sub_init(void);
static void trans_sub_pull(void);

static void send_rc_dbus_data(const rc_dbus_obj_t* rc);

/* ==================== 全局变量 ==================== */
static uint32_t heart_dt;
static float trans_dt;
static float trans_start;
static float yaw_filtered = 0;
static float pitch_filtered = 0;
static float yaw_obs = 0;
TeamColor team_color = UNKNOWN;

/* ==================== 接收数据缓冲区（新增）==================== */
// 存储接收到的原始数据
static uint8_t received_data_buffer[MOTOR_MAX_DATA_LEN];
static uint16_t received_data_length = 0;
static uint8_t data_ready_flag = 0;  // 数据就绪标志

/**
 * @brief 获取接收到的数据
 * @param data 输出缓冲区
 * @param length 输出数据长度
 * @return 1=有新数据, 0=无新数据
 */
uint8_t get_received_data(uint8_t *data, uint16_t *length)
{
    if (data_ready_flag)
    {
        memcpy(data, received_data_buffer, received_data_length);
        *length = received_data_length;
        data_ready_flag = 0;  // 清除标志
        return 1;
    }
    return 0;
}

/**
 * @brief 检查是否有新数据
 */
uint8_t has_new_data(void)
{
    return data_ready_flag;
}

/* ==================== CRC16查找表 (CRC16-CCITT) ==================== */
static const uint16_t CRC16_TABLE[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50A5, 0x60C6, 0x70E7,
    0x8108, 0x9129, 0xA14A, 0xB16B, 0xC18C, 0xD1AD, 0xE1CE, 0xF1EF,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52B5, 0x4294, 0x72F7, 0x62D6,
    0x9339, 0x8318, 0xB37B, 0xA35A, 0xD3BD, 0xC39C, 0xF3FF, 0xE3DE,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64E6, 0x74C7, 0x44A4, 0x5485,
    0xA56A, 0xB54B, 0x8528, 0x9509, 0xE5EE, 0xF5CF, 0xC5AC, 0xD58D,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76D7, 0x66F6, 0x5695, 0x46B4,
    0xB75B, 0xA77A, 0x9719, 0x8738, 0xF7DF, 0xE7FE, 0xD79D, 0xC7BC,
    0x48C4, 0x58E5, 0x6886, 0x78A7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xC9CC, 0xD9ED, 0xE98E, 0xF9AF, 0x8948, 0x9969, 0xA90A, 0xB92B,
    0x5AF5, 0x4AD4, 0x7AB7, 0x6A96, 0x1A71, 0x0A50, 0x3A33, 0x2A12,
    0xDBFD, 0xCBDC, 0xFBBF, 0xEB9E, 0x9B79, 0x8B58, 0xBB3B, 0xAB1A,
    0x6CA6, 0x7C87, 0x4CE4, 0x5CC5, 0x2C22, 0x3C03, 0x0C60, 0x1C41,
    0xEDAE, 0xFD8F, 0xCDEC, 0xDDCD, 0xAD2A, 0xBD0B, 0x8D68, 0x9D49,
    0x7E97, 0x6EB6, 0x5ED5, 0x4EF4, 0x3E13, 0x2E32, 0x1E51, 0x0E70,
    0xFF9F, 0xEFBE, 0xDFDD, 0xCFFC, 0xBF1B, 0xAF3A, 0x9F59, 0x8F78,
    0x9188, 0x81A9, 0xB1CA, 0xA1EB, 0xD10C, 0xC12D, 0xF14E, 0xE16F,
    0x1080, 0x00A1, 0x30C2, 0x20E3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83B9, 0x9398, 0xA3FB, 0xB3DA, 0xC33D, 0xD31C, 0xE37F, 0xF35E,
    0x02B1, 0x1290, 0x22F3, 0x32D2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xB5EA, 0xA5CB, 0x95A8, 0x8589, 0xF56E, 0xE54F, 0xD52C, 0xC50D,
    0x34E2, 0x24C3, 0x14A0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xA7DB, 0xB7FA, 0x8799, 0x97B8, 0xE75F, 0xF77E, 0xC71D, 0xD73C,
    0x26D3, 0x36F2, 0x0691, 0x16B0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xD94C, 0xC96D, 0xF90E, 0xE92F, 0x99C8, 0x89E9, 0xB98A, 0xA9AB,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18C0, 0x08E1, 0x3882, 0x28A3,
    0xCB7D, 0xDB5C, 0xEB3F, 0xFB1E, 0x8BF9, 0x9BD8, 0xABBB, 0xBB9A,
    0x4A75, 0x5A54, 0x6A37, 0x7A16, 0x0AF1, 0x1AD0, 0x2AB3, 0x3A92,
    0xFD2E, 0xED0F, 0xDD6C, 0xCD4D, 0xBDAA, 0xAD8B, 0x9DE8, 0x8DC9,
    0x7C26, 0x6C07, 0x5C64, 0x4C45, 0x3CA2, 0x2C83, 0x1CE0, 0x0CC1,
    0xEF1F, 0xFF3E, 0xCF5D, 0xDF7C, 0xAF9B, 0xBFBA, 0x8FD9, 0x9FF8,
    0x6E17, 0x7E36, 0x4E55, 0x5E74, 0x2E93, 0x3EB2, 0x0ED1, 0x1EF0
};

/**
 * @brief 计算CRC16 (CRC16-CCITT, 初始值0xFFFF)
 */
static uint16_t calculate_crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++)
    {
        uint8_t index = (crc >> 8) ^ data[i];
        crc = (crc << 8) ^ CRC16_TABLE[index];
    }
    return crc;
}

/* ==================== 接收相关 ==================== */
static uint8_t rx_buffer[512];
static uint16_t rx_index = 0;

typedef enum {
    WAIT_FOR_HEADER1,
    WAIT_FOR_HEADER2,
    RECEIVING_LENGTH,
    RECEIVING_DATA
} RxState_e;

static RxState_e rx_state = WAIT_FOR_HEADER1;
static uint16_t expected_data_length = 0;

/* ==================== 上发数据类型定义 ==================== */

typedef enum
{
    TRANS_MSG_ID_STM32_STATUS = 0x01,    // STM32状态数据（批量上传：IMU+弹速+模式+遥控器+电机）
    TRANS_MSG_ID_VISION_COMMAND = 0x02,  // 视觉控制命令（批量下发：云台控制）
} trans_msg_id_e;

/**
 * @brief 发送STM32状态数据（批量上传）
 * 包含：IMU四元数 + 弹速和模式
 */
static void send_stm32_status(void)
{
    uint8_t data[32];
    size_t offset = 0;

    // 消息ID
    data[offset++] = (uint8_t)TRANS_MSG_ID_STM32_STATUS;

    // ========== 1. IMU数据（四元数）：16字节 ==========
    // 假设你有全局的IMU数据，这里用占位符
    // 你需要替换为实际的IMU数据源
    float imu_qx = 0.0f, imu_qy = 0.0f, imu_qz = 0.0f, imu_qw = 1.0f;
    // TODO: 从你的IMU模块获取实际数据
    // imu_qx = gim_ins.quat.x;
    // imu_qy = gim_ins.quat.y;
    // imu_qz = gim_ins.quat.z;
    // imu_qw = gim_ins.quat.w;
    
    memcpy(data + offset, &imu_qx, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &imu_qy, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &imu_qz, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &imu_qw, sizeof(float)); offset += sizeof(float);

    // ========== 2. 弹速和模式：6字节 ==========
    float bullet_speed = 15.0f;  // TODO: 从传感器获取实际弹速
    uint8_t robot_mode = 1;      // TODO: 从状态机获取实际模式 (0=idle, 1=auto_aim, 2=small_buff, 3=big_buff, 4=outpost)
    uint8_t shoot_mode = 2;      // TODO: 从状态机获取射击模式 (0=left_shoot, 1=right_shoot, 2=both_shoot)
    
    memcpy(data + offset, &bullet_speed, sizeof(float)); offset += sizeof(float);
    data[offset++] = robot_mode;
    data[offset++] = shoot_mode;

    // 总长度：1(ID) + 16(IMU) + 6(弹速模式) = 23字节
    send_custom_data(data, (uint16_t)offset);
}

/* ==================== 发送函数 ==================== */

/**
 * @brief 通用发送函数（内部使用）
 */
static void send_packet(uint8_t *data, uint16_t length)
{
    if (usb_tx_queue == NULL || data == NULL)
    {
        return;
    }

    if (length > USB_TX_PAYLOAD_MAX)
    {
        length = USB_TX_PAYLOAD_MAX;
    }

    usb_tx_msg_t *msg = NULL;
    if (usb_tx_free_queue == NULL)
    {
        return;
    }

    if (xQueueReceive(usb_tx_free_queue, &msg, 0) != pdPASS || msg == NULL)
    {
        return;
    }

    size_t offset = 0;
    
    // 1. 帧头
    msg->data[offset++] = MOTOR_FRAME_HEADER_1;
    msg->data[offset++] = MOTOR_FRAME_HEADER_2;
    
    // 2. 数据长度
    memcpy(msg->data + offset, &length, sizeof(uint16_t));
    offset += sizeof(uint16_t);
    
    // 3. 数据
    memcpy(msg->data + offset, data, length);
    offset += length;
    
    // 4. 计算CRC16（从帧头到数据结束）
    uint16_t crc = calculate_crc16(msg->data, offset);
    memcpy(msg->data + offset, &crc, sizeof(uint16_t));
    offset += sizeof(uint16_t);

    msg->len = (uint16_t)offset;
    
    // 5. 发送
    if (xQueueSend(usb_tx_queue, &msg, 0) != pdPASS)
    {
        (void)xQueueSend(usb_tx_free_queue, &msg, 0);
    }
}

static void usb_tx_pump(void)
{
    if (usb_tx_queue == NULL)
    {
        return;
    }

    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceHS.pClassData;
    if (hcdc == NULL)
    {
        return;
    }

    if (hcdc->TxState != 0U)
    {
        return;
    }

    if (usb_tx_inflight != NULL)
    {
        (void)xQueueSend(usb_tx_free_queue, &usb_tx_inflight, 0);
        usb_tx_inflight = NULL;
    }

    if (usb_tx_inflight == NULL)
    {
        (void)xQueueReceive(usb_tx_queue, &usb_tx_inflight, 0);
    }

    if (usb_tx_inflight == NULL)
    {
        return;
    }

    (void)CDC_Transmit_HS(usb_tx_inflight->data, usb_tx_inflight->len);
}

/**
 * @brief 发送IMU数据（四元数）
 * @param x, y, z, w 四元数分量
 */
void send_imu_data(float x, float y, float z, float w)
{
    uint8_t data[17];  // 1 + 4*4 = 17字节
    size_t offset = 0;
    
    // 消息ID
    data[offset++] = TRANS_MSG_ID_IMU_DATA;
    
    // 四元数
    memcpy(data + offset, &x, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &y, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &z, sizeof(float)); offset += sizeof(float);
    memcpy(data + offset, &w, sizeof(float)); offset += sizeof(float);
    
    send_packet(data, offset);
}

/**
 * @brief 发送弹速和模式信息
 * @param bullet_speed 弹速 (m/s)
 * @param mode 模式 (0=idle, 1=auto_aim, 2=small_buff, 3=big_buff, 4=outpost)
 * @param shoot_mode 射击模式 (0=left_shoot, 1=right_shoot, 2=both_shoot)
 */
void send_bullet_speed(float bullet_speed, uint8_t mode, uint8_t shoot_mode)
{
    uint8_t data[7];  // 1 + 4 + 1 + 1 = 7字节
    size_t offset = 0;
    
    // 消息ID
    data[offset++] = TRANS_MSG_ID_BULLET_SPEED;
    
    // 弹速
    memcpy(data + offset, &bullet_speed, sizeof(float)); offset += sizeof(float);
    
    // 模式
    data[offset++] = mode;
    data[offset++] = shoot_mode;
    
    send_packet(data, offset);
}


/**
 * @brief 发送自定义数据
 */
void send_custom_data(uint8_t *data, uint16_t length)
{
    send_packet(data, length);
}

/**
 * @brief 发送浮点数数组（最简单）
 */
void send_float_array(float *floats, uint8_t count)
{
    uint8_t data[256];
    size_t offset = 0;
    
    for (uint8_t i = 0; i < count; i++)
    {
        memcpy(data + offset, &floats[i], sizeof(float));
        offset += sizeof(float);
    }
    
    send_packet(data, offset);
}

/* ==================== 接收函数 ==================== */

/**
 * @brief USB数据接收处理
 */
void process_usb_data(uint8_t* Buf, uint32_t *Len)
{
    if (usb_rx_queue == NULL || Buf == NULL || Len == NULL || *Len == 0)
    {
        return;
    }

    uint32_t in_len = *Len;
    if (in_len > USB_RX_MSG_LEN)
    {
        in_len = USB_RX_MSG_LEN;
    }

    // 从静态池中获取一个消息对象
    usb_rx_msg_t *msg = &usb_rx_msg_pool[usb_rx_msg_idx];
    msg->len = (uint16_t)in_len;
    memcpy(msg->data, Buf, in_len);

    // 发送消息指针。注意：这里发送的是池中对象的指针。
    // usb_rx_msg_idx 简单自增实现轮转（Circular Buffer 思想）
    {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        if (xQueueSendFromISR(usb_rx_queue, &msg, &xHigherPriorityTaskWoken) == pdPASS)
        {
            usb_rx_msg_idx = (usb_rx_msg_idx + 1) % USB_RX_MSG_COUNT;
        }
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

static void process_usb_bytes(uint8_t* Buf, uint16_t Len)
{
    for (uint16_t i = 0; i < Len; i++)
    {
        uint8_t byte = Buf[i];

        switch (rx_state)
        {
            case WAIT_FOR_HEADER1:
                if (byte == MOTOR_FRAME_HEADER_1)
                {
                    rx_index = 0;
                    rx_buffer[rx_index++] = byte;
                    rx_state = WAIT_FOR_HEADER2;
                }
                break;

            case WAIT_FOR_HEADER2:
                if (byte == MOTOR_FRAME_HEADER_2)
                {
                    rx_buffer[rx_index++] = byte;
                    rx_state = RECEIVING_LENGTH;
                }
                else
                {
                    rx_state = WAIT_FOR_HEADER1;
                }
                break;

            case RECEIVING_LENGTH:
                rx_buffer[rx_index++] = byte;
                if (rx_index >= 4)  // 帧头(2) + 长度(2)
                {
                    memcpy(&expected_data_length, rx_buffer + 2, sizeof(uint16_t));
                    rx_state = RECEIVING_DATA;
                }
                break;

            case RECEIVING_DATA:
                rx_buffer[rx_index++] = byte;

                // 检查是否接收完整：帧头(2) + 长度(2) + 数据(N) + CRC16(2)
                if (rx_index >= 4 + expected_data_length + 2)
                {
                    // 验证CRC16
                    size_t crc_offset = 4 + expected_data_length;
                    uint16_t received_crc;
                    memcpy(&received_crc, rx_buffer + crc_offset, sizeof(uint16_t));

                    uint16_t calculated_crc = calculate_crc16(rx_buffer, crc_offset);

                    if (received_crc == calculated_crc)
                    {
                        // CRC校验通过，解析数据
                        uint8_t *data = rx_buffer + 4;  // 跳过帧头和长度

                        // ========== 保存接收到的数据（新增）==========
                        // 复制数据到全局缓冲区供外部使用
                        memcpy(received_data_buffer, data, expected_data_length);
                        received_data_length = expected_data_length;
                        data_ready_flag = 1;  // 设置数据就绪标志

                        // ========== 解析数据 ==========
                        if (expected_data_length >= 1)
                        {
                            size_t offset = 0;
                            uint8_t msg_id = data[offset++];

                            switch (msg_id)
                            {
                                case TRANS_MSG_ID_VISION_COMMAND:
                                {
                                    // 解析视觉控制命令：control(1) + shoot(1) + yaw(4) + pitch(4) = 11字节
                                    if (expected_data_length >= 1 + 1 + 1 + 4 + 4)
                                    {
                                        uint8_t control_flag = data[offset++];
                                        uint8_t shoot_flag = data[offset++];
                                        
                                        float yaw, pitch;
                                        memcpy(&yaw, data + offset, sizeof(float)); offset += sizeof(float);
                                        memcpy(&pitch, data + offset, sizeof(float)); offset += sizeof(float);
                                        
                                        // TODO: 在这里添加你的云台控制逻辑
                                        // 例如：设置云台目标角度
                                        // gimbal_set_target(yaw, pitch, control_flag, shoot_flag);
                                        
                                        // 存储到反馈数据（如果需要的话）
                                        trans_fdb_data.control_flag = control_flag;
                                        trans_fdb_data.shoot_flag = shoot_flag;
                                        trans_fdb_data.target_yaw = yaw;
                                        trans_fdb_data.target_pitch = pitch;
                                    }
                                    break;
                                }

                                case TRANS_MSG_ID_RC_DBUS:
                                {
                                    // 如果需要接收遥控器数据的话
                                    // 添加解析逻辑
                                    break;
                                }

                                default:
                                {
                                    // 未知消息ID
                                    break;
                                }
                            }
                        }
                    }

                    // 重置状态
                    rx_state = WAIT_FOR_HEADER1;
                    rx_index = 0;
                }
                break;
        }
    }
}

/**
 * @brief 获取接收到的数据
 */
struct trans_fdb_msg* get_trans_fdb(void)
{
    return &trans_fdb_data;
}

/* ==================== 任务函数 ==================== */

void trans_task_init(void)
{
    memset(&trans_fdb_data, 0, sizeof(trans_fdb_data));
    rx_state = WAIT_FOR_HEADER1;
    rx_index = 0;

    if (usb_rx_queue == NULL)
    {
        usb_rx_queue = xQueueCreate(USB_RX_MSG_COUNT, sizeof(usb_rx_msg_t *));
    }

    if (usb_tx_queue == NULL)
    {
        usb_tx_queue = xQueueCreate(USB_TX_MSG_COUNT, sizeof(usb_tx_msg_t *));
    }

    if (usb_tx_free_queue == NULL)
    {
        usb_tx_free_queue = xQueueCreate(USB_TX_MSG_COUNT, sizeof(usb_tx_msg_t *));
        if (usb_tx_free_queue != NULL)
        {
            for (uint32_t i = 0; i < USB_TX_MSG_COUNT; i++)
            {
                usb_tx_msg_t *p = &usb_tx_msg_pool[i];
                (void)xQueueSend(usb_tx_free_queue, &p, 0);
            }
        }
    }

    // 初始化订阅
    trans_sub_init();
    heart_dt = dwt_get_time_ms();
}

void trans_control_task(void)
{
    trans_start = dwt_get_time_ms();
    
    // 订阅数据更新
    trans_sub_pull();

    // 处理消息队列中的所有消息
    usb_rx_msg_t *msg = NULL;
    while (xQueueReceive(usb_rx_queue, &msg, 0) == pdPASS)
    {
        if (msg != NULL && msg->len > 0)
        {
            process_usb_bytes(msg->data, msg->len);
        }
    }

    usb_tx_pump();
    
    /* ==================== 心跳检测 ==================== */
    if ((dwt_get_time_ms() - heart_dt) >= HEART_BEAT)
    {
        heart_dt = dwt_get_time_ms();
        // 可以在这里发送心跳包
    }
    
    /* ==================== 发送数据 ==================== */

    /* 批量上传STM32状态数据（IMU + 弹速模式） */
    send_stm32_status();

    /* ==================== 发布数据更新 ==================== */
    trans_pub_push();
    
    /* ==================== 性能监测 ==================== */
    trans_dt = dwt_get_time_ms() - trans_start;
    if (trans_dt > 1)
    {
        // LOGINFO("Transmission Task is being DELAY! dt = [%f]\r\n", &trans_dt);
    }
}

/* ==================== 消息订阅发布 ==================== */

static void trans_pub_push(void)
{
    mcn_publish(MCN_HUB(transmission_fdb_topic), &trans_fdb_data);
}

static void trans_sub_init(void)
{
    ins_topic_node = mcn_subscribe(MCN_HUB(ins_topic), NULL, NULL);
    chassis_cmd_node = mcn_subscribe(MCN_HUB(chassis_cmd), NULL, NULL);
    gimbal_cmd_node = mcn_subscribe(MCN_HUB(gimbal_cmd), NULL, NULL);
    gimbal_fdb_node = mcn_subscribe(MCN_HUB(gimbal_fdb_topic), NULL, NULL);
    gimbal_ins_node = mcn_subscribe(MCN_HUB(gimbal_ins_topic), NULL, NULL);
    chassis_motor_trans_node= mcn_subscribe(MCN_HUB(chassis_motor_trans_topic), NULL, NULL);
}

static void trans_sub_pull(void)
{
    if (mcn_poll(ins_topic_node))
    {
        mcn_copy(MCN_HUB(ins_topic), ins_topic_node, &ins);
    }
    if (mcn_poll(chassis_cmd_node))
    {
        mcn_copy(MCN_HUB(chassis_cmd), chassis_cmd_node, &chass_cmd);
    }
    if (mcn_poll(gimbal_cmd_node))
    {
        mcn_copy(MCN_HUB(gimbal_cmd), gimbal_cmd_node, &gimbal_cmd);
    }
    if (mcn_poll(gimbal_fdb_node))
    {
        mcn_copy(MCN_HUB(gimbal_fdb_topic), gimbal_fdb_node, &gimbal_fdb);
    }
    if (mcn_poll(gimbal_ins_node))
    {
        mcn_copy(MCN_HUB(gimbal_ins_topic), gimbal_ins_node, &gim_ins);
    }
    if (mcn_poll(chassis_motor_trans_node))
    {
        mcn_copy(MCN_HUB(chassis_motor_trans_topic), chassis_motor_trans_node, &chassis_motor_msg);
    }
}
