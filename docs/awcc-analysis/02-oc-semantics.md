# AWCC 逆向分析 — 02 OC 语义

> 证据来源:OC Controls 1.0.4.0 反编译源(`AWCC/decomp/`,文件路径即出处)。只记录行为与参数。

## 1. 双通道 OC 架构

| 通道 | 执行者 | 用途 |
|---|---|---|
| BIOS(WMI) | DomOCBiosSupportAPI.dll → AWCC WMI 方法 13/14/15 | OC 总开关、Failsafe 状态、UI/BIOS 控制权协商 |
| OS(Intel XTU) | XtuService.exe(COM 本地服务)+ XtuCoreServer.dll | CPU 功耗墙/倍频/电压/电流限制的实际写入 |

**结论:AWCC 5.x 在 G 系列上的"超频"= BIOS 放权 + XTU 写 PL/倍频/电压。** GPU 侧走 NVAPI(不在本包内)。

## 2. BIOS OC 状态(WMI 方法 13/14/15,来源 BIOSSupportProvider.cs)

`ReturnOverclockingReport()`(WMI 方法 13)返回 int 位域:
- `byte0`(bits 0–7):CPU OC 使能(1=开)
- `byte1`(bits 8–15):OC UI BIOS 控制(1=已放权;**0=不支持 OC UI 控制**)
- `byte2`(bits 16–23):OC Failsafe 已触发(1=已触发)

`SetOCUIBIOSControl(bool)`(方法 14):请求 BIOS 把 OC 控制权交给 OS 侧 UI;byte1==0 的机型直接跳过。
`ClearOCFailSafeFlag()`(方法 15):**Dell 应用在读到 Failsafe 置位时会自动调用清除**,然后靠 UI 提示用户(即 Failsafe 是"一次性锁存 + 软件清除",不是硬重启锁)。

## 3. XTU 控制 ID(来源 Intel.Overclocking.SDK.Common/ControlIdHelper.cs)

dell-toolbox 关心的子集(完整表 0–102+ 见源文件):

| ID | 名称 | 含义 |
|---|---|---|
| 47 | TurboPackageTdpShort | **PL2**(短时睿频功耗墙) |
| 48 | TurboPackageTdpExtended | **PL1**(持续功耗墙) |
| 49 | TurboPackageTdpShortEnable | PL2 时窗使能 |
| 66 | TurboExtendedTimeWindow | PL2 时间窗(tau) |
| 51/52/53 | TurboIaCorePowerLimit/Enable/Lock | IA 核功耗限制 |
| 57/58 | TurboIa/GfxCoreCurrentMaximum | 电流限制 |
| 102 | ProcessorCoreIccMax | 核心电流上限 |
| 29–32,42,43 | TurboOne..SixCoreMaxTurboRatio | 每核簇最大倍频 |
| 34 | CpuVoltageOffset | 核心电压偏移 |
| 83 | GraphicsCoreVoltageOffset | iGPU 电压偏移 |

## 4. 机型限制数据(DataConfig.occ,来源 DataConfigurationReader.cs)

- 本地路径:`%ProgramData%\Alienware\OCControls\DataConfig.occ`(XML,XSD 生成类 DataConfig*)
- 字段:`Model`;`Fan`{Type, Min, Max}(风扇百分比的合法范围);`ThrottlingIgnored`(温度/功耗/电流节流忽略开关);`Frequency`/`CoreVoltage`/`VoltageOffset` 各为 `Level[]`(档位表,UI 滑条的刻度来源);签名设置防篡改
- OTA 下载:`http://dellupdater.dell.com/non_du/alienware/`(首次联网按机型拉取;OTAOCDataSetupTool.exe 执行)
- 档案(ProfileData*.cs)存 SSCE 3.5 数据库,字段含 OCEnabled、multiplier、voltage、voltageOffset、iCCMax、cacheICCMax、power、per-core 倍频数组

## 5. 对 dell-toolbox 的落地结论

1. **G3 3590(i5-9300H/i7-9750H,倍频锁定)**:可行 OC 面 = PL1/PL2(± tau)+ IccMax;倍频/电压偏移在多数 Dell BIOS 上锁死 → v1 只做 PL1/PL2,与 Core 的 `ControlTargets.cpuPl1W/cpuPl2W` 对应。
2. 实现路径二选一:(a) 走 WMI?——**不行**,WMI 只管 BIOS OC 状态,PL 写入是 XTU 私有 COM;(b) 自研 MSR/PPM 写入(0x610 MSR PL1/PL2)或调用 XTU CLI。v1 先保留 targets、暂不落盘(见 Controller 注释),真机阶段选型。
3. OC UI 使能协商照抄:SetOCUIBIOSControl(true) → 轮询 ReturnOverclockingReport 确认;Failsafe 置位时读一次清一次 + 通知用户。
4. 风扇范围钳制用 DataConfig 的 Fan Min/Max(默认 0/100,待真机抓 occ 文件核对)。

## 6. 官方 Dell Power Manager 3.18.0 逆向补充(2026-09-20)

来源:`Dell-Power-Manager-Service_2963M_WIN64_3.18.0_A00.EXE` → MSI → ilspycmd(smblib/objlib/utilities 程序集)。

### 6.1 DPM 的 BIOS 通道(与 AWCC WMI 不同!)
- `root\wmi` 命名空间,类 **`BDat`**(数据缓冲)+ **`BFn`**(函数分发,方法 `DoBFn`,`Data` 属性为二进制缓冲,最小 36 字节)
- 这是 Dell ACPI-WMI 私有二进制协议(DAInterface.ExecuteDACommand),带 class/selector 白名单(1/4/8/17)
- **dell-toolbox 暂不复刻此协议**(无真机联调条件);充电配置走 cctk 桥,doctor 探测 BFn/BDat 类是否存在(为将来接入积累情报)

### 6.2 充电模式语义(官方枚举,DPM 内部)
Standard=1, Express=2, PredominatelyAC=3, Auto=4, Custom=5(带 start/stop 阈值), PermanentLongLife=6, AdvancedCharge=7
→ 与 cctk `--PrimaryBattChargeCfg`(adaptive/standard/express/primacuse/custom)对齐:standard↔1, express↔2, primacuse↔3, adaptive↔4, custom↔5

### 6.3 热模式(DPM UI 层)
Optimized/Cool/Quiet/Ultra Performance(Flags 1/2/4/8)→ BIOS 层经由 BFn 通道;在 AWCC 机型上等价于 USTT 档(dell-toolbox 用 AWCC WMI 通道实现同一 UI 语义)

### 6.4 MSBatteryClass(电池信息,已落地)
`root\wmi` `MSBatteryClass` 单类全字段:PowerOnline/Charging/Discharging/ChargeRate/DischargeRate(mW)/Voltage(mV)/CycleCount/DesignedCapacity/FullChargedCapacity/RemainingCapacity(mWh)/DeviceName/ManufactureName/SerialNumber/Chemistry
percent = Remaining/FullCharged;health = FullCharged/Designed —— 与 Dell Power Manager 显示口径一致

## 7. PL1/PL2 硬件写入:官方流程与安全边界(实现前必读,2026-09-20)

用户可见行为:AWCC 的 OC/性能页对 PL1(持续)/PL2(爆发)的修改是**真实的 CPU 功率状态写入**,不是记录值。

### 7.1 官方调用链(AWCC 5.x)
1. `Return_OverclockingReport`(WMI method 13,入参 0)——读回位字段:byte0=CPU OC 是否启用、byte1=BIOS UI 控制位、byte2=**OC failsafe 标志**(BIOS 检测到异常重启后置位,提示用户恢复默认)
2. `Set_OCUIBIOSControl`(method 14)——真正的写入。控制点(Intel XTU 语义):47=PL2、48=PL1、49=PL2 enable、34=CPU 电压 offset、102=IccMax;一次调用按 DataConfig 打包多个控制点
3. `Clear_OCFailSafeFlag`(method 15)——正常退出/恢复默认时清除 failsafe 标志
4. 数值边界:全部钳制在 `DataConfig.occ` 的范围表内(来源 DataConfigurationReader.cs),UI 不可能发超界值

### 7.2 安全边界(我们的实现必须遵守)
- **必须先 method 13 读回并确认 failsafe=0**,非 0 时拒绝写入并提示恢复
- **钳制**:PL1 ∈ [BIOS 默认-20W, BIOS 默认+10W] 起步(真机标定后放宽);永远 PL2 ≥ PL1
- **步进写入**:每次只动一个点,写后 method 13 读回校验,不匹配即回滚
- **被动模式(AWCC 运行)禁止写入**——AWCC 独占 OC 通道
- **掉电安全**:OC failsafe 是 BIOS 级的,异常重启会自动回默认;我们的 UI 要识别该状态(13 的 byte2)并提示

### 7.3 分阶段真机验证计划
1. 阶段 A(只读):method 13 读回,验证位字段与 AWCC UI 状态一致
2. 阶段 B:PL1 单点 ±5W,烤机验证频率/功耗/温度响应,确认 failsafe 不置位
3. 阶段 C:PL2 与完整滑条 + 配置持久化 + 重启恢复测试
