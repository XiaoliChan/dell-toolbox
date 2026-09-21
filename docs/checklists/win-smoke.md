# Windows 真机冒烟清单(G3 3590)

每次发版前在真机走一遍;任何一步失败即阻断发版。

## 安装与启动
- [ ] 解压 portable zip 到任意目录,双击 `dell-toolbox.exe`,UAC 通过
- [ ] 冷启动到主窗口出现 < 1s(对比 Dell Power Manager 数秒)
- [ ] 无 `dell-toolbox.log` 中的 error 行(`--verbose` 检查)

## doctor
- [ ] `dell-toolbox.exe --doctor`:thermal PASS 且 fans: 2;charge 若 FAIL 有明确指引
- [ ] 无管理员权限运行 → UAC 或明确失败信息(不应静默)

## 热控制
- [ ] 模式三选切换:托盘图标温度刷新,风扇行为变化可闻
- [ ] Custom 模式拖曲线点 → 松手后 ≤2s 风扇转速变化(2% 死区内不抖动)
- [ ] 曲线含 ≤2 点/重复温度时被拒绝(UI 不允许生成)
- [ ] 重启应用后曲线/模式/场景参数保留(dell-toolbox.ini)

## 场景检测
- [ ] scene 参数加 `game.exe`,运行任一游戏 → 10s 内 active 策略变 scene、模式切 G-Mode
- [ ] 关游戏 → 30s 后回到 baseline/Balanced
- [ ] GPU 60%+ 压力测试(gfurmark/3DMark)同样触发;55–60% 区间不反复切换

## Failsafe
- [ ] 压力测试将 CPU 推到 ≥95°C 持续 8s → 模式强制 G-Mode,active=failsafe
- [ ] 降温到 <90°C(<80°C GPU)持续 60s → 恢复原模式
- [ ] 拔掉温度读数(如暂停 WMI)→ failsafe 行为保守(视为危险)

## 性能页
- [ ] PL1/PL2 滑条 → active=manual,HWiNFO 核验 PL1 生效(v1 若未接 XTU 则标注)
- [ ] Resume auto → 回到 scene/baseline
- [ ] 手动 30 分钟后自动过期回自动策略

## 电池页
- [ ] 充电模式切换后 `cctk --PrimaryBattChargeCfg` 输出一致
- [ ] Custom 起止阈值差 <5 被强制拉开;写入后 cctk 回读正确

## 退出
- [ ] 托盘 Exit → 模式恢复 Balanced,进程退出,无残留高转速
- [ ] 关闭窗口 → 藏到托盘,提示一次

## 自启
- [ ] Settings 开自启 → `schtasks /query /tn DellToolbox` 存在
- [ ] 注销重登 → 进程以 --minimized 启动且托盘可见

## 启动失败排查(side-by-side / 打不开)
1. **必须整目录删除后重新解压**,不要在旧目录里覆盖解压(旧 DLL/清单残留会复现旧错误)
2. 若仍报 "side-by-side configuration is incorrect":
   - 打开 事件查看器 → Windows 日志 → 应用程序,找来源为 **SideBySide** 的最新条目,截图发回(内含确切解析错误行号)
   - 管理员 cmd 跑 `sfc /scannow` 修复组件存储后再试
   - 或装一次 [VC++ 2015-2022 x64 运行库](https://aka.ms/vs/17/release/vc_redist.x64.exe)(包内已自带,此步只为排除系统存储损坏)
3. 报 "找不到 VCRUNTIME140.dll":说明解压时 DLL 丢失/被杀软隔离,检查 zip 完整性与杀软白名单
