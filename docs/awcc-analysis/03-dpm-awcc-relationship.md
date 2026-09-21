# Dell Power Manager ↔ AWCC 关系与共存策略

> 来源:DPM 3.18.0 与 OC Controls 1.0.4.0 反编译 + tcc-g15 行为对照。结论用于 dell-toolbox 的共存设计。

## 1. 两者的域划分(为什么会共存不冲突)

| 域 | AWCC 5.x | Dell Power Manager |
|---|---|---|
| 通道 | `root\WMI` AWCCWmiMethodFunction(Thermal_Information/Thermal_Control) | `root\wmi` BFn/BDat(Dell ACPI-WMI 二进制协议)+ ComponentService |
| 风扇 | ✅ 完全拥有(模式+转速) | ❌ 不碰 |
| 热模式 | ✅ 四档 USTT + G-Mode | ⚠️ 也提供(Optimized/Cool/Quiet/Ultra),写 BIOS 的另一条路 |
| 电池信息 | ❌ | ✅ MSBatteryClass 读 |
| 充电模式 | ❌ | ✅ BFn Set/Get(class 8, selector 18/19) |
| OS 电源计划 | ✅(Fusion Power Management 页) | ✅ |
| 常驻循环 | ✅ AWCC 主程序 + AWCCService(约 1s 轮询,会重写档位) | ⚠️ DPM 只在改设置时写;DPM Service 主要做通知/策略 |

**关键结论:风扇/热模式是 AWCC 的独占地盘;充电/电池是 DPM 的地盘。冲突只发生在"双方都写热模式"或"两个风扇控制器同时跑闭环"。**

## 2. dell-toolbox 的共存设计(v0.4.0 起)

1. **检测**:4s 周期枚举进程,发现 `awcc.exe / awccservice.exe / awccscheduler.exe / alienware*` 即视为 AWCC 在运行。
2. **让权(monitor-only)**:AWCC 运行期间 Controller 进入 passive——继续读传感器喂 UI,但不写任何模式/转速,避免两个闭环打架(Dashboard 顶部横幅提示)。
3. **自动恢复**:AWCC 退出后自动收回控制权。
4. **充电通道**与 DPM 用同一条 BFn 路径:只响应用户显式操作,无常驻写,天然共存。
5. 热模式的**读回**是 AWCC WMI 的盲区(协议无 get-mode 操作),所以 UI 状态以"我们最后一次成功写入"为准;AWCC 若中途改档,横幅机制会让用户优先处理冲突。

## 3. 对"tray 切 G-Mode 但 AWCC 没反应"的解释

G3 3590 支持 G-Mode(tcc-g15 写 0xAB 后 AWCC G 标志会亮——用户实测)。v0.3.1 及之前 dell-toolbox 绑定了 AWCCWmiMethodFunction 的第一个 WMI 实例,而固件可能暴露多个实例、只有其中一个真正执行;加上 ExecMethod 合成的 ReturnValue 属性可能掩盖真实错误码,导致写入"看似成功实则没生效"。
v0.4.0 修复:遍历所有实例直到真实成功 + 跳过 ReturnValue + 每次模式写入记日志(写 0xAB 时 BIOS 的返回值在 dell-toolbox.log 可查)。

## 4.【实证补充 2026-09-20】DPM 热模式的真实写入路径(逐字节)

来源:SystemInterop.cs + ThermalTables.cs + ThermalManagementProvider.cs(全部反编译自官方 3.18.0)。

DPM 的四档(Optimized/Cool/Quiet/Ultra)走的是 **DA 通道的 class 17**:
- GET:`Class=17, Selector=19, cbArg1=0` → 返回 `cbRes2` 低字节 = **支持档位位掩码**(bit0=Balanced/Optimized, bit1=Cool, bit2=Quiet, bit3=Ultra),`cbRes3` 低字节 = **当前模式(0–8)**,`cbRes3[3]` = 风扇故障标志(ThermalTables.Parse)
- SET:`Class=17, Selector=19, cbArg1=1, cbArg2=模式值(1/2/4/8)`(SetUsttInformation)

命名证据:DPM 源码里这对调用就叫 **`USTT_GET/SET_THERMAL_INFORMATION`**(常量名),与 AWCC WMI 文档里的 `USTT_Balanced/USTT_Performance/USTT_Cool/USTT_Quiet` 同名——**两家的固件热引擎都叫 USTT**。

## 5. 结论与不确定性(明确区分)

**已实证:**
1. DPM 和 AWCC 用**完全不同的 WMI 通道**(DPM: BFn/BDat class 17;AWCC: AWCCWmiMethodFunction.Thermal_Control),互不调用对方。
2. DPM 从不碰 AWCCWmiMethodFunction;AWCC OC Controls 从不碰 BFn——域划分在代码层面成立。
3. 两边的固件接口都以"USTT"命名,都是 四档结构。

**推断(未在真机验证,标注为待测):**
4. 两个通道是**同一固件 USTT 引擎的两个前端**(编码不同:DPM 发 1/2/4/8 小索引;AWCC 发 0x96–0xA3 模式字节)。"DPM 写 Quiet 后 AWCC UI 变 Quiet"尚无直接证据。
5. 由此,dell-toolbox 用 AWCC 通道实现的四档,与 DPM 四档**语义对齐但编码不同**;切换到 G-Mode 时 DPM 的 DA 通道没有对应概念(G 档只在 AWCC 通道,0xAB)。

**验证方法(下次真机测试,10 分钟):**
- dell-toolbox 选 Quiet → 打开 DPM Thermal Management 页,看当前档位是否显示 Quiet(若 DPM 动态刷新)
- 反向:DPM 选 Ultra → dell-toolbox doctor/重进页面看写入是否被覆盖
- doctor 现在会打印 BFn/BDat/WMI_Query 是否存在 → 确认这台 G3 的 DA 通道是否可用(决定充电通道能否脱离 cctk)


## 6. 当前档位读回(v0.5.0 落地 + G-Mode 已知限制)

- **四档读回已落地**:`BFn` class 17 selector 19(cbArg1=0)→ `cbRes3` 低字节 = 当前档位(1/2/4/8)。dell-toolbox 每 tick 读取,发现与本地不同即"采纳外部变更":同步 UI 单选、托盘勾选,并发气泡通知。这就是 DT 实时跟随 AWCC/DPM 四档修改的机制。
- **G-Mode 读回受限**:DA 的档位值域只有 0–8,G 档(AWCC 0xAB)不在其中,DA 读回显示为空。候选方案是 AWCC WMI 方法 37 `GameShiftStatus(arg2)`——但语义未确证(可能是切换而非读取),盲调有把用户 G 档打开/关闭的风险。待真机验证后再启用(验证方法:先读 argr,写 G 档前后对比)。

## 7.【真机实测 2026-09-20,G3 3590】AWCC 对热模式的真实行为

用户对照实验(tcc-g15 与 AWCC 同时运行):
1. tcc-g15 写 G-Mode → **AWCC 界面不显示**(AWCC 不读外部写入)
2. G-Mode 生效时 tcc-g15 写 Balanced → **几秒内被弹回 G-Mode** → **AWCC 运行时会周期性重申自己的档位**
3. AWCC 切换档位 → 外部可检测到变化

**结论:AWCC 运行 = 它独占热模式写入权(外部写入被覆盖)。**
dell-toolbox 策略(v0.5.2):检测到 AWCC 进程即自动转只读监控(4s 周期),AWCC 退出自动恢复写入;转/恢复各发一次气泡。Custom 风扇闭环同理受此约束。
