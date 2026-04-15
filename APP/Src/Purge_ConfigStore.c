#include "Purge_ConfigStore.h"

#include <stddef.h>
#include <string.h>

#include "main.h"
#include "Purge_Control.h"
#include "stm32f4xx_hal_flash.h"
#include "stm32f4xx_hal_flash_ex.h"

/*
 * Purge_ConfigStore
 *
 * 该模块负责将上位机设置过的关键工艺参数持久化到 STM32F427 内部 Flash，
 * 以便设备掉电后重新上电时，仍然可以恢复上一次保存过的参数。
 *
 * 当前设计原则：
 * 1. 只保存“会被上位机改写、且需要掉电保留”的配置参数
 * 2. 读写流程尽量独立，不和协议层、状态机层耦合
 * 3. Flash 中存储的数据带 magic / version / checksum 校验
 * 4. 写入完成后做一次回读校验，避免“看似写成功、实际内容已损坏”
 *
 * 扇区选择说明：
 * 当前 IAR 工程配置的是 STM32F427VG，内部 Flash 容量为 1 MB。
 * 这里使用最后一个扇区 Sector 11 作为参数区：
 *   0x080E0000 - 0x080FFFFF
 *
 * 注意：
 * 该扇区必须从链接脚本中排除，不能再被程序正文或常量区占用。
 */
#define PURGE_CONFIG_FLASH_ADDRESS   0x080E0000UL
#define PURGE_CONFIG_FLASH_SECTOR    FLASH_SECTOR_11
#define PURGE_CONFIG_FLASH_BANK      FLASH_BANK_1

/* 固定标识，用于判断当前扇区里是不是本模块写入的数据。 */
#define PURGE_CONFIG_MAGIC           0x50434647UL
/* 参数结构版本号，后续结构变化时可用于兼容升级。 */
#define PURGE_CONFIG_VERSION         0x00000001UL

/*
 * Flash 中实际落盘的数据镜像。
 *
 * 字段说明：
 * 1. magic / version / length
 *    用于识别结构类型、版本和长度，避免误读旧数据或脏数据
 * 2. 中间参数区
 *    仅保存当前需要掉电保留的配置参数
 * 3. checksum
 *    对前面所有字段做校验，防止 Flash 内容损坏后被误用
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t length;

    float fill_flow_lpm;
    float run_flow_lpm;
    float run_enter_o2_percent;
    float run_exit_humidity_percent;
    float max_inlet_pressure_bar;
    float min_inlet_pressure_bar;
    float max_outlet_pressure_bar;
    float min_outlet_pressure_bar;
    uint32_t fill_time_ms;

    uint32_t external_output_flag;
    uint32_t gas_type;

    uint32_t checksum;
} PurgeConfigStoreImage_t;

/* 从当前 g_purge_ctrl 生成一份待写入 Flash 的镜像。 */
static void PurgeConfigStore_FillImage(PurgeConfigStoreImage_t *image);
/* 将一份已经校验通过的镜像内容应用到 g_purge_ctrl。 */
static void PurgeConfigStore_ApplyImage(const PurgeConfigStoreImage_t *image);
/* 校验 Flash 镜像是否合法、完整。 */
static uint8_t PurgeConfigStore_IsImageValid(const PurgeConfigStoreImage_t *image);
/* 计算镜像校验值。 */
static uint32_t PurgeConfigStore_CalcChecksum(const PurgeConfigStoreImage_t *image);
/* 获取 Flash 参数区首地址处的镜像指针。 */
static const PurgeConfigStoreImage_t *PurgeConfigStore_GetFlashImage(void);

/*
 * 上电恢复参数。
 *
 * 流程：
 * 1. 直接把 Flash 参数区映射为结构体指针
 * 2. 做完整合法性校验
 * 3. 校验通过后，回填到 g_purge_ctrl
 *
 * 如果校验失败，则保持控制层默认参数不变。
 */
uint8_t PurgeConfigStore_Load(void)
{
    const PurgeConfigStoreImage_t *image = PurgeConfigStore_GetFlashImage();

    if (PurgeConfigStore_IsImageValid(image) == 0U)
    {
        return 0U;
    }

    PurgeConfigStore_ApplyImage(image);
    return 1U;
}

/*
 * 保存当前参数到 Flash。
 *
 * 流程：
 * 1. 先从 g_purge_ctrl 生成一份镜像
 * 2. 如果 Flash 中已有完全相同的数据，则直接返回成功，避免无意义擦写
 * 3. 解锁 Flash，擦除目标扇区
 * 4. 按 32-bit word 顺序写入整份镜像
 * 5. 锁回 Flash
 * 6. 再次回读并校验，确认写入内容真实有效
 *
 * 这里使用“整扇区擦除 + 整镜像重写”的简单方案，
 * 好处是实现稳定、逻辑清晰，适合当前参数规模不大的场景。
 */
uint8_t PurgeConfigStore_Save(void)
{
    FLASH_EraseInitTypeDef erase_init;
    PurgeConfigStoreImage_t image;
    const PurgeConfigStoreImage_t *flash_image;
    const PurgeConfigStoreImage_t *verify_image;
    uint32_t sector_error = 0U;
    uint32_t address;
    uint32_t word_index;
    uint32_t word_count;
    HAL_StatusTypeDef status = HAL_OK;

    PurgeConfigStore_FillImage(&image);
    flash_image = PurgeConfigStore_GetFlashImage();

    /* 内容完全一致时，不重复擦写，减少 Flash 擦写次数。 */
    if ((PurgeConfigStore_IsImageValid(flash_image) != 0U) &&
        (memcmp(flash_image, &image, sizeof(image)) == 0))
    {
        return 1U;
    }

    /* 配置擦除参数：擦除 Sector 11 整个扇区。 */
    (void)memset(&erase_init, 0, sizeof(erase_init));
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.Banks = PURGE_CONFIG_FLASH_BANK;
    erase_init.Sector = PURGE_CONFIG_FLASH_SECTOR;
    erase_init.NbSectors = 1U;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;

    /* 先解锁，再清理上一轮可能残留的状态标志。 */
    HAL_FLASH_Unlock();
    __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP |
                           FLASH_FLAG_OPERR |
                           FLASH_FLAG_WRPERR |
                           FLASH_FLAG_PGAERR |
                           FLASH_FLAG_PGPERR |
                           FLASH_FLAG_PGSERR |
                           FLASH_FLAG_RDERR);

    /* 先擦除，再逐字写入镜像。 */
    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    if (status == HAL_OK)
    {
        address = PURGE_CONFIG_FLASH_ADDRESS;
        word_count = (uint32_t)(sizeof(image) / sizeof(uint32_t));
        for (word_index = 0U; word_index < word_count; word_index++)
        {
            uint32_t data_word = ((const uint32_t *)&image)[word_index];
            status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, address, data_word);
            if (status != HAL_OK)
            {
                break;
            }
            address += sizeof(uint32_t);
        }
    }

    /* 无论成功失败都先重新上锁，防止后续误操作 Flash。 */
    HAL_FLASH_Lock();
    if (status != HAL_OK)
    {
        return 0U;
    }

    /* 写完后立即回读并校验，确保 Flash 里实际内容正确。 */
    verify_image = PurgeConfigStore_GetFlashImage();
    if ((PurgeConfigStore_IsImageValid(verify_image) == 0U) ||
        (memcmp(verify_image, &image, sizeof(image)) != 0))
    {
        return 0U;
    }

    return 1U;
}

/* 从运行态参数生成一份 Flash 镜像。 */
static void PurgeConfigStore_FillImage(PurgeConfigStoreImage_t *image)
{
    if (image == 0)
    {
        return;
    }

    (void)memset(image, 0, sizeof(*image));

    image->magic = PURGE_CONFIG_MAGIC;
    image->version = PURGE_CONFIG_VERSION;
    image->length = (uint32_t)sizeof(*image);

    image->fill_flow_lpm = g_purge_ctrl.fill_flow_lpm;
    image->run_flow_lpm = g_purge_ctrl.run_flow_lpm;
    image->run_enter_o2_percent = g_purge_ctrl.run_enter_o2_percent;
    image->run_exit_humidity_percent = g_purge_ctrl.run_exit_humidity_percent;
    image->max_inlet_pressure_bar = g_purge_ctrl.max_inlet_pressure_bar;
    image->min_inlet_pressure_bar = g_purge_ctrl.min_inlet_pressure_bar;
    image->max_outlet_pressure_bar = g_purge_ctrl.max_outlet_pressure_bar;
    image->min_outlet_pressure_bar = g_purge_ctrl.min_outlet_pressure_bar;
    image->fill_time_ms = g_purge_ctrl.fill_time_ms;
    image->external_output_flag = (g_purge_ctrl.external_output_flag != 0U) ? 1UL : 0UL;
    image->gas_type = (uint32_t)g_purge_ctrl.gas_type;

    image->checksum = PurgeConfigStore_CalcChecksum(image);
}

/* 将一份已通过校验的镜像恢复到控制层配置结构体。 */
static void PurgeConfigStore_ApplyImage(const PurgeConfigStoreImage_t *image)
{
    if (image == 0)
    {
        return;
    }

    g_purge_ctrl.fill_flow_lpm = image->fill_flow_lpm;
    g_purge_ctrl.run_flow_lpm = image->run_flow_lpm;
    g_purge_ctrl.run_enter_o2_percent = image->run_enter_o2_percent;
    g_purge_ctrl.run_exit_humidity_percent = image->run_exit_humidity_percent;
    g_purge_ctrl.max_inlet_pressure_bar = image->max_inlet_pressure_bar;
    g_purge_ctrl.min_inlet_pressure_bar = image->min_inlet_pressure_bar;
    g_purge_ctrl.max_outlet_pressure_bar = image->max_outlet_pressure_bar;
    g_purge_ctrl.min_outlet_pressure_bar = image->min_outlet_pressure_bar;
    g_purge_ctrl.fill_time_ms = image->fill_time_ms;
    g_purge_ctrl.external_output_flag = (uint8_t)((image->external_output_flag != 0U) ? 1U : 0U);
    g_purge_ctrl.gas_type = (image->gas_type == (uint32_t)XCDA) ? XCDA : N2;
}

/*
 * 校验镜像是否可用。
 *
 * 依次检查：
 * 1. magic 是否匹配
 * 2. version 是否匹配
 * 3. length 是否正确
 * 4. 枚举/布尔字段是否落在允许范围
 * 5. checksum 是否一致
 */
static uint8_t PurgeConfigStore_IsImageValid(const PurgeConfigStoreImage_t *image)
{
    if (image == 0)
    {
        return 0U;
    }

    if (image->magic != PURGE_CONFIG_MAGIC)
    {
        return 0U;
    }

    if (image->version != PURGE_CONFIG_VERSION)
    {
        return 0U;
    }

    if (image->length != (uint32_t)sizeof(*image))
    {
        return 0U;
    }

    if (image->external_output_flag > 1UL)
    {
        return 0U;
    }

    if ((image->gas_type != (uint32_t)N2) &&
        (image->gas_type != (uint32_t)XCDA))
    {
        return 0U;
    }

    if (image->checksum != PurgeConfigStore_CalcChecksum(image))
    {
        return 0U;
    }

    return 1U;
}

/*
 * 计算镜像校验值。
 *
 * 这里采用轻量级 FNV-1a 32-bit 哈希，对 checksum 字段之前的所有字节计算。
 * 它不是加密校验，只用于检测 Flash 数据是否被破坏。
 */
static uint32_t PurgeConfigStore_CalcChecksum(const PurgeConfigStoreImage_t *image)
{
    const uint8_t *data = (const uint8_t *)image;
    uint32_t hash = 2166136261UL;
    size_t index;
    size_t length = offsetof(PurgeConfigStoreImage_t, checksum);

    for (index = 0U; index < length; index++)
    {
        hash ^= data[index];
        hash *= 16777619UL;
    }

    return hash;
}

/* 返回 Flash 参数区首地址对应的镜像指针。 */
static const PurgeConfigStoreImage_t *PurgeConfigStore_GetFlashImage(void)
{
    return (const PurgeConfigStoreImage_t *)PURGE_CONFIG_FLASH_ADDRESS;
}
