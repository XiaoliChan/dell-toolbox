# AWCC 逆向分析 — 01 热调度

> 证据来源:tcc-g15 `WMI-AWCC-doc.md`(协议)+ OC Controls 反编译(边界)+ 对官方行为的推断(已标注)。AWCC 主程序本体在加密 bundle 内未能静态展开(见 00),故本文区分【实证】与【推断】。

## 1.【实证】热控制协议(AWCC WMI)

协议细节以 `../tcc-g15/src/Backend/AWCCWmiWrapper.py` 与其 `WMI-AWCC-doc.md` 为准:
- 类 `AWCCWmiMethodFunction`(root\WMI),方法 `Thermal_Information` / `Thermal_Control` / `GetFanSensors`,单 int 入参打包 op<<0 | id<<8 | idx<<16
- 模式:Custom=0,Balanced=0x97(部分机型需 USTT 0xA0 补丁),G-Mode=0xAB;SetAddonSpeedPercent=(spd<<16)|(fan<<8)|2
- 传感器 0x01–0x30,风扇 0x31–0x63;错误返回 -1/0xFFFFFFFF

**读取侧操作码(2026-09-20 补充,第二证据源:Linux 内核 `alienware-wmi` 驱动)**:
- `Thermal_Information` op `0x0B`、arg=0 → **读当前激活模式**,返回模式码全表(枚举 `awcc_thermal_profile`):0x00 Custom,0x96/0xA3 Quiet,0x97/0xA0 Balanced,0x98/0x99/0xA1/0xA4 Performance,0xA2 Cool,0xA5 低功耗,**0xAB G-Mode**。内核 `platform_profile_get` 即用此读 —— 这是唯一能看到 AWCC 所设 G-Mode 的读路径(DA 通道 0-8 表达不了)
- `GameShiftStatus`(方法 37/0x25):op `0x01` = **切换(toggle,读路径严禁发)**,op `0x02` = **纯读取**(0 = 关,1 = G-Mode 开)
- `Thermal_Control` op `0x01` = 激活模式(与现有写路径一致)

dell-toolbox 的 `hal/win/WinThermalHAL` 已逐条镜像这些常量,读回顺序:AWCC op 0x0B → GameShiftStatus GET → DA 通道兜底。
- **G-Mode 是 EC 闩锁(真机实锤 2026-09-21)**:闩锁开着时写其他模式字节,EC 几秒内把 0xAB 压回——表现为"切走又弹回 G-Mode"+外部采纳气泡,与 AWCC 是否安装无关。正确离开方式 = 先 GameShiftStatus toggle 关闩锁再写目标模式;进入 G-Mode 反之。内核 alienware-wmi 的 platform_profile set 即此逻辑

## 2.【实证】OC Controls 侧的热边界

- 风扇控制只有范围钳制:`DataConfig.occ` 的 Fan{Min,Max}(来源 DataConfigurationReader.cs);曲线本体在 AWCC 主程序
- `ThermalModes` 枚举仅 Normal/Aggressive(XTU 节流态度,非风扇模式)
- OCControlsWindowsService 只做 WCF 服务宿主(XTU 代理、加密、BIOS 支持、监控)+ 通知,**没有闭环风扇调度循环** —— 调度循环在 AWCC 主程序进程内

## 3.【推断】官方调度的可观察行为(无法静态证实,真机阶段用 procmon/ETW 标定)

- AWCC 常驻时以约 1Hz 轮询温度并在 G-Mode/Balanced 间切换(BIOS 曲线,不逐 tick 写转速);Custom 模式才逐 tick 写 SetAddonSpeedPercent
- Game Shift(G 系 Fn+G)是**手动**开关;AWCC 6.x 才有游戏库联动自动切换
- tcc-g15 的缺陷正是"一次性设置、无常驻闭环":dell-toolbox 用常驻 1Hz Controller + Failsafe(继承 tcc-g15 95/85°C×8s)+ 双阈值场景检测补齐,见设计文档 §5

## 4. dell-toolbox 调度参数(与官方对齐点)

| 参数 | 值 | 来源 |
|---|---|---|
| 循环周期 | 1s | 推断自官方行为,兼顾 WMI 写冻结风险 |
| 模式写 | 变更即写,不节流 | 实证:Thermal_Control 无幂等代价 |
| 风扇写死区 | 2% + ≥1s 间隔 | tcc-g15 经验(WMI 写可致瞬时卡顿) |
| 曲线滞回 | 3°C(降沿) | 通用工程实践,官方未证实 |
| Failsafe | CPU95/GPU85°C × 8s → G-Mode;<90/80°C × 60s 恢复 | tcc-g15 fail-safe 语义 |
