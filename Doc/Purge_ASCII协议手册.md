# Purge ASCII协议手册

## 1. 文档说明

本文档按 [协议格式.txt](d:/SJWORK/purge/Purge_N2/Doc/协议格式.txt) 中定义的标准格式，重新整理 `Purge_N2` 项目的 ASCII 通信协议。

本文档当前目标是统一协议定义标准，供固件、上位机和联调共同参考。

## 2. 通信基本规则

### 2.1 方向定义

- `Host -> Device`
  主机下发查询、控制、参数设置和参数读取命令。
- `Device -> Host`
  设备返回查询结果、命令应答、报警上报和事件上报。

### 2.2 报文结束符

所有报文均以 ASCII 文本发送，并以：

```text
<CR><LF>
```

结束。

说明：

- `<CR>` 为回车，ASCII `0x0D`
- `<LF>` 为换行，ASCII `0x0A`

### 2.3 空格规则

按当前定义，字段之间使用空格分隔，示例如下：

```text
FSR FC=0<CR><LF>
HCS START_PURGE POD<CR><LF>
ARS WARNING POS_PRESS_ABNORMAL ALID=0x00000010<CR><LF>
```

建议主机和设备严格按本文档格式发送，不额外插入多余空格。

## 3. 查询指令 Host -> Device

## 3.1 查询总格式

```text
FSR FC=xx<CR><LF>
```

字段说明：

- `FSR`
  Function Status Request，查询请求报文头。
- `FC`
  Function Code，查询功能码。
- `xx`
  查询项编号。

### 3.1.1 FC 定义

- `FC=0`
  查询设备运行状态。
- `FC=1`
  查询各传感器当前数据。
- `FC=2`
  查询当前配置参数。

## 3.2 FC=0 查询运行状态

### 请求

```text
FSR FC=0<CR><LF>
```

### 应答

```text
FSD=0 STATUS=xx MODE=xx FAULT=xx CYCLE=xx<CR><LF>
```

字段说明：

- `FSD=0`
  Function Status Data，表示状态查询结果，`0` 对应运行状态查询。
- `STATUS`
  当前状态机状态。
  建议取值：`INIT`、`SELF_CHECK`、`STANDBY`、`FILL`、`RUN`、`FAULT`。
- `MODE`
  当前工作腔体。
  取值：`POD` 或 `MICRO`。
- `FAULT`
  当前故障码，建议使用十六进制字符串表示。
- `CYCLE`
  当前已执行的 purge 流程计数。

## 3.3 FC=1 查询传感器数据

### 请求

```text
FSR FC=1<CR><LF>
```

### 应答

```text
FSD=1 O2=xx FLOW=xx AFLOW=xx INP=xx OUTP=xx T=xx H=xx<CR><LF>
```

字段说明：

- `FSD=1`
  传感器数据查询结果。
- `O2`
  氧浓度。
  建议单位：`%`。
- `FLOW`
  进气流量反馈值。
  建议单位：`LPM`。
- `AFLOW`
  出气流量值。
  建议单位：`LPM`。
- `INP`
  入口压力。
  单位：`Bar`。
- `OUTP`
  出口压力。
  单位：`Bar`。
- `T`
  温度。
  建议单位：`°C`。
- `H`
  湿度。
  建议单位：`%RH`。

## 3.4 FC=2 查询配置参数

### 请求

```text
FSR FC=2<CR><LF>
```

### 应答

```text
FSD=2 FILLFLOW=80.00 RUNFLOW=50.00 TARGETO2=5.00 TARGETHUMI=10.00 POS_PRESS_MAX=7.00 POS_PRESS_MIN=5.00 NEG_PRESS_MAX=-0.70 NEG_PRESS_MIN=-0.90 EXTERNAL_OUTPUT=0 GAS_TYPE=N2<CR><LF>
```

字段说明：

- `FSD=2`
  配置参数查询结果。
- `FILLFLOW`
  充气置换阶段流量设定值。
  单位：`LPM`。
- `RUNFLOW`
  运行阶段流量设定值。
  单位：`LPM`。
- `TARGETO2`
  氧浓度目标阈值。
  单位：`%`。
- `TARGETHUMI`
  湿度目标阈值。
  单位：`%RH`。
- `POS_PRESS_MAX`
  正压上限。
  单位：`Bar`。
- `POS_PRESS_MIN`
  正压下限。
  单位：`Bar`。
- `NEG_PRESS_MAX`
  负压上限。
  单位：`Bar`。
- `NEG_PRESS_MIN`
  负压下限。
  单位：`Bar`。
- `EXTERNAL_OUTPUT`
  外部输出开关状态。
  `0` 表示关闭，`1` 表示开启。
- `GAS_TYPE`
  充气气体类型。
  取值：`N2` 或 `XCDA`。

## 4. 控制指令 Host -> Device

## 4.1 START_PURGE POD

### 请求

```text
HCS START_PURGE POD<CR><LF>
```

### 含义

对 `POD` 腔体执行充气置换。

### 应答

```text
HCA XX<CR><LF>
```

`XX` 取值说明：

- `OK`
  指令执行成功。
- `BUSY`
  设备忙。
- `ALARM`
  当前设备处于报警/故障状态，拒绝执行。
- `DENEID`
  设备拒绝执行，或该指令未定义。

## 4.2 START_PURGE MICRO

### 请求

```text
HCS START_PURGE MICRO<CR><LF>
```

### 含义

对微环境腔体执行充气置换。

### 应答

```text
HCA XX<CR><LF>
```

`XX` 含义同上。

## 4.3 STOP_PURGE

### 请求

```text
HCS STOP_PURGE<CR><LF>
```

### 含义

停止当前充气置换流程。

### 应答

```text
HCA XX<CR><LF>
```

`XX` 取值：`OK`、`BUSY`、`ALARM`、`DENEID`。

## 5. 参数设置指令 Host -> Device

## 5.1 设置参数总格式

```text
ECS XX=Data<CR><LF>
```

字段说明：

- `ECS`
  Equipment Config Set，参数设置请求。
- `XX`
  参数名。
- `Data`
  参数值。

## 5.2 支持的参数项

- `FILLFLOW`
  腔体充气流量。
- `RUNFLOW`
  运行流量。
- `TARGETO2`
  目标氧浓度。
- `TARGETHUMI`
  目标湿度。
- `POS_PRESS_MAX`
  正压上限。
- `POS_PRESS_MIN`
  正压下限。
- `NEG_PRESS_MAX`
  负压上限。
- `NEG_PRESS_MIN`
  负压下限。
- `EXTERNAL_OUTPUT`
  外部输出开关。
- `GAS_TYPE`
  充气气体类型。

### `GAS_TYPE` 说明

- `N2`
  使用氮气充气。完成判据需要考虑氧浓度和湿度。
- `XCDA`
  使用 XCDA 气体充气。完成判据只考虑湿度，不考虑氧浓度。

## 5.3 参数设置应答

### 请求示例

```text
ECS FILLFLOW=80.00<CR><LF>
ECS TARGETO2=5.00<CR><LF>
ECS GAS_TYPE=N2<CR><LF>
```

### 应答

```text
ECA XX<CR><LF>
```

字段说明：

- `ECA`
  Equipment Config Ack，参数设置应答。
- `XX`
  应答结果。

`XX` 取值：

- `OK`
  设置成功。
- `BUSY`
  设备忙。
- `ALARM`
  当前设备处于报警状态，拒绝设置。

## 6. 参数读取指令 Host -> Device

## 6.1 读取参数总格式

```text
ECR XX<CR><LF>
```

字段说明：

- `ECR`
  Equipment Config Read，参数读取请求。
- `XX`
  目标参数名。

## 6.2 支持读取的参数项

支持项与 `ECS` 相同：

- `FILLFLOW`
- `RUNFLOW`
- `TARGETO2`
- `TARGETHUMI`
- `POS_PRESS_MAX`
- `POS_PRESS_MIN`
- `NEG_PRESS_MAX`
- `NEG_PRESS_MIN`
- `EXTERNAL_OUTPUT`
- `GAS_TYPE`

## 6.3 参数读取应答

### 请求示例

```text
ECR FILLFLOW<CR><LF>
ECR GAS_TYPE<CR><LF>
```

### 应答

```text
ECD XX=Data<CR><LF>
```

字段说明：

- `ECD`
  Equipment Config Data，参数读取返回。
- `XX`
  参数名。
- `Data`
  当前参数值。

示例：

```text
ECD FILLFLOW=80.00<CR><LF>
ECD GAS_TYPE=XCDA<CR><LF>
```

## 7. 事件上报开关指令 Host -> Device

## 7.1 请求格式

```text
EDER XX<CR><LF>
```

字段说明：

- `EDER`
  Event Data Enable Request，事件上报开关请求。
- `XX`
  控制值。

`XX` 取值：

- `ON`
  开启事件上报。
- `OFF`
  关闭事件上报。

## 7.2 应答格式

```text
EERA XX<CR><LF>
```

字段说明：

- `EERA`
  Event Enable Response Ack，事件上报开关应答。
- `XX`
  应答结果。

`XX` 取值：

- `OK`
  执行成功。
- `BUSY`
  设备忙。
- `ALARM`
  当前设备处于报警状态，拒绝执行。

## 8. 报警事件上报 Device -> Host

## 8.1 报文格式

```text
ARS WARNING MSG ALID=xx<CR><LF>
```

字段说明：

- `ARS`
  Alarm Report Send，报警上报。
- `WARNING`
  报警级别字段。当前标准固定为 `WARNING`。
- `MSG`
  报警名称。
- `ALID`
  报警码。

## 8.2 报警名称定义

- `POS_PRESS_ABNORMAL`
  正压异常。
- `NEG_PRESS_ABNORMAL`
  负压异常。
- `O2_ABNORMAL`
  氧浓度异常。
- `TEMP_HUMI_ABNORMAL`
  温湿度异常。
- `FLOW_ABNORMAL`
  进气流量异常。
- `AFLOW_ABNORMAL`
  出气流量异常。
- `PURGE_TIMEOUT`
  气体置换超时。
- `DEVICE_FAULT`
  设备故障。

## 8.3 报警码定义

- `0x00000001`
  控制流程兜底故障。
- `0x00000002`
  O2 传感器长时间无有效更新。
- `0x00000004`
  流量反馈长时间无效。
- `0x00000008`
  温湿度传感器长时间无效。
- `0x00000010`
  正压侧超出设定范围。
- `0x00000020`
  负压侧超出设定范围。
- `0x00000040`
  RUN 阶段 O2 再次升高。
- `0x00000080`
  置换流程超时。
- `0x00000100`
  RUN 阶段动态补偿超时后，O2 或湿度仍未恢复。

### 示例

```text
ARS WARNING POS_PRESS_ABNORMAL ALID=0x00000010<CR><LF>
ARS WARNING PURGE_TIMEOUT ALID=0x00000080<CR><LF>
```

## 9. 事件上报 Device -> Host

## 9.1 报文格式

```text
AERS MSG<CR><LF>
```

字段说明：

- `AERS`
  Auto Event Report Send，事件上报。
- `MSG`
  事件名称。

## 9.2 事件名称定义

- `START_PURGE`
  充气开始。
- `CMPL_PURGE_MIC`
  微环境充气完成。
- `CMPL_PURGE_POD`
  POD 腔体充气完成。
- `STOP_PURGE`
  充气停止。
- `POWER_UP`
  设备上电。
- `CMPL_SET`
  参数设置完成。

### 示例

```text
AERS START_PURGE<CR><LF>
AERS CMPL_PURGE_POD<CR><LF>
AERS STOP_PURGE<CR><LF>
```

## 10. 联调建议

- 查询命令建议优先使用 `FSR FC=0`、`FSR FC=1`、`FSR FC=2` 验证设备状态、传感器和配置。
- 控制命令建议先发 `HCS START_PURGE POD` 或 `HCS START_PURGE MICRO`，再观察 `AERS` 和 `ARS` 上报。
- 使用 `ECS` 修改参数后，建议立刻用 `ECR` 或 `FSR FC=2` 做回读确认。
- 使用 `GAS_TYPE=XCDA` 时，上位机不应再用氧浓度作为完成判据。

## 11. 说明

本文档是按你提供的协议定义标准重写的“协议手册版”。

如果你下一步要我继续做，我可以直接接着帮你两件事：

1. 按这份新协议标准，把 [Purge_HostComm.c](d:/SJWORK/purge/Purge_N2/APP/Src/Purge_HostComm.c) 的实际解析和回包格式一起改掉。
2. 把这份手册再整理成更正式的表格版，对上位机开发会更直观。
