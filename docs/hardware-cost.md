# 12 硬件市场价格与启动投资估算

> 目标：为「FPGA 单卡（可扩展多卡）加速 DeepSeek-V4-Flash」项目（见 [11_FPGA加速平台与硬件配置方案](./11_FPGA加速平台与硬件配置方案.md)）给出**当前市场价**与**启动投资资金**测算。
> 价格基准：2026 年市场调研（含实价锚点与估算），币种人民币（¥），汇率按 1 USD ≈ ¥7.15。**所有价格为估算，采购前须向渠道/代理二次询价确认。**

---

## 1. 市场价格锚点（调研所得）

| 硬件/许可 | 市场价 | 类型 | 来源/依据 |
| --- | --- | --- | --- |
| AMD Alveo U55C（16GB HBM2e 计算卡） | **¥82,828/张** | ✅ 实价锚点 | [东南大学 2024-12 竞价采购成交价](https://www.dingbiao.com/zhaobiao/70186c651c1cfc0ba73ee4830b6389ee.html)（含上门安装） |
| AMD Versal HBM VHK158 评估套件（EK-VHK158-G） | **$16,554 ≈ ¥118,000/套** | ✅ 实价锚点 | [ICSuns 报价](https://www.icsuns.com/zh-cn/product/ek-vhk158-g/22147154.html)；[Farnell/Newark 在售](https://de.farnell.com/en-DE/amd/ek-vhk158-g-j/evaluation-kit-arm-cortex-a72/dp/4531168) |
| AMD Alveo U280（8GB HBM2 + 64GB DDR4） | ≈ ¥45,000/张 | 估算 | 已停产，二手/渠道价；新机时代 ~$5–6K |
| AMD Alveo U250（64GB DDR4） | ≈ ¥20,000/张 | 估算 | 二手渠道价；[渠道参考](https://expresscomputersystems.com/collections/all/products/xilinx-alveo-u250-dw-fh-3-4-length-225w-passive-accelerator-card-a-u250-p64g-pq-g) |
| AMD Versal AI Core VCK5000 | ≈ ¥85,000/张 | 估算 | ~$12K，渠道价 |
| 工作站（单卡）：Ryzen 9 / Xeon W，64GB，2TB NVMe | ≈ ¥20,000 | 估算 | 品牌整机/自组 |
| 4 卡服务器：2U 双路 EPYC，256GB，4TB NVMe | ≈ ¥70,000 | 估算 | 含冗余电源 |
| 8 卡服务器：4U 双路 EPYC，512GB，8TB NVMe | ≈ ¥120,000 | 估算 | 8× 全高全长卡槽位 + 冗余 2400W |
| Vivado ML Enterprise 浮动许可 | ≈ ¥21,500/年 | 估算 | [$2,995–3,595/年（AMD 官方渠道）](https://adaptivesupport.amd.com/s/question/0D54U00005wRQ4cSAG/)；**Vivado ML Standard 免费**（多数器件够用，见 §5 许可说明） |
| 附件（PCIe riser/线材/工具） | ¥5,000–10,000 | 估算 | 单卡 ¥5K / 多卡 ¥10K |

> 关键结论：**U55C 与 VHK158 单卡价差约 ¥3.6 万**（U55C ≈ ¥8.3 万，VHK158 ≈ ¥11.8 万）；全量 284B 需要 4–8 张 HBM 卡，硬件投入进入数十万~百万级。

## 2. 投资档位测算（启动资金）

| 档位 | 硬件清单 | 硬件小计 | 含 10% 预备金 |
| --- | --- | --- | --- |
| **T0 开发起步（无 FPGA）** | Linux 工作站/云主机（阶段一~三：环境、RTL、difftest、基准全部可完成） | ¥20,000 | ¥22,000 |
| **T1 经济单卡（U55C）** | 1× U55C + 工作站 + Vivado 免费档 + 附件 | ¥107,828 | ¥118,611 |
| **T2 基准单卡（VHK158，推荐主交付）** | 1× VHK158 + 工作站 + Vivado Enterprise（如需要）+ 附件 | ¥164,811 | ¥181,292 |
| **T3 经济全量（4× VHK158）** | 4× VHK158 + 4 卡服务器 + Vivado Enterprise + 附件 | ¥574,894 | ¥632,384 |
| **T4 旗舰全量（8× VHK158）** | 8× VHK158 + 8 卡服务器 + Vivado Enterprise + 附件 | ¥1,098,339 | ¥1,208,173 |

（T0 无 FPGA 即可覆盖大赛 PR 模板的阶段一~三；阶段四 FPGA 演示按 T1/T2 起步，全量 284B 按 T3/T4。）

## 3. 建议启动路线（分阶段投入）

| 阶段 | 投入项 | 追加资金 | 累计资金 | 可交付 |
| --- | --- | --- | --- | --- |
| ① 阶段一~三（0–3 个月） | 工作站/云主机（T0） | ~¥2 万 | ~¥2 万 | 环境、vdot RTL、difftest、B0–B3 基准（emu 口径）、设计文档 |
| ② 阶段四 FPGA 起步 | 1× U55C（T1） | ~¥10.8 万 | ~¥12.8 万 | 单卡小型化模型演示（30–60 token/s） |
| ③ 升级主交付（可选） | 换 1× VHK158（T2） | 追加 ~¥5.7 万 | ~¥18.5 万 | 单卡 ≥50 token/s（主交付达标） |
| ④ 全量 284B（可选加分） | 4 卡（T3）/ 8 卡（T4） | ~¥41–93 万 | ~¥60–112 万 | 全量 INT2/INT4 演示（20–80 token/s） |

> **建议**：若预算有限，**按 ①→② 路线**（启动资金 ~¥3 万，加 FPGA 后累计 ~¥13 万）即可完整交付大赛作品；T2 的 VHK158 是主交付达标（≥50 token/s）的最稳妥选择；T3/T4 属旗舰扩展，建议以"先单卡验证、后租用/分期扩展"方式推进。
> **F2 时代纯云路线（2026-08 更新，见 §5.1）**：AWS F2 已提供带 16GB HBM 的云 FPGA，主交付与全量演示均可按小时租用——**纯云方案总投入 ≈ ¥5–8 万**（阶段一~三 ¥2 万 + F2 开发/演示 ¥1–5 万），无需购置任何实体 FPGA 卡；若需长期自用再考虑购置（§5.1.2 判断）。

## 4. 口径与假设

1. **价格性质**：U55C（竞价成交价，含安装）与 VHK158（渠道报价）为实价锚点；其余为渠道/二手市场估算，含税口径，实际以代理报价为准。
2. **汇率**：1 USD ≈ ¥7.15（2026 市场水平）；美元计价项按此折算。
3. **许可**：Vivado ML **Standard 免费**，覆盖多数器件与基本综合/实现/仿真；**Enterprise**（~$3K/年）用于部分器件（如 Versal Premium/HBM 系列某些特性）与高级功能。⚠️ 2026 年起 AMD 免费层级对 **Linux 支持有变动**（[参考](https://blog.hotdry.top/posts/2026/05/24/amd-vivado-linux-tier-deprecation-impact/)），本项目为 Linux 工作流，**采购前须确认所选器件与 Vivado 版本的许可/OS 支持**；必要时预算 Enterprise 许可或评估开源流程（Yosys/nextpnr 对 Versal 支持有限，慎用）。
4. **NEMU/emu 工具链**：全部开源免费（Verilator/NEMU/difftest），不产生许可费。
5. **人力与差旅**：未计入（视团队情况另列）；大赛无硬件强制要求，阶段一~三零硬件成本即可完成。
6. **折旧/二手**：Alveo U280/U250 已停产，二手市场活跃，可作为**开发期替代卡**（验证 vdot 通路）显著降本；但 HBM 容量不足无法跑主交付模型，仅作通路验证。

## 5. 省钱与备选策略

| 策略 | 说明 | 可省 |
| --- | --- | --- |
| 二手 U250/U280 做通路验证 | 先验证 vdot 执行单元与数据通路，再上 HBM 卡 | ~¥5–8 万 |
| **云上 FPGA 租赁（见 §5.1）** | 开发/回归/CI 用云 FPGA 按量或月付，避免购卡前期投入 | ~¥8–10 万前期 |
| 云主机跑阶段一~三 | 按需开机，避免购机闲置 | ~¥1–2 万 |
| 免费 Vivado ML + 开源仿真 | 综合/实现用免费档，仿真用 Verilator | ~¥2.2 万/年 |
| 先单卡后扩展 | 全量方案在单卡验证通过后再采购多卡/租赁 | 避免过早沉没成本 |
| 国产替代调研 | 紫光同创/复旦微 HBM 型号（若生态可接受） | 视型号，通常更低 |
| 借力高校/实验室资源 | 大赛/课题组常可借用 FPGA 服务器 | 视情况 |

## 5.1 云上 FPGA 租赁方案（F2 时代：可以，且主交付也可云化）

> **重大更新（2026-08）**：**AWS F1 已退役**（AWS 以第二代 F2 实例逐步替代，F1 新实例启动受限，不再作为规划基准——[f1 实例状态参考](https://go.runs-on.com/instances/ec2/f1)、[re:Post 相关问答](https://repost.aws/ko/questions/QUNaAz4HY5TSO7_SBNRy1mtA/help-with-f1-2xlarge-instance-launch)）。新一代 **F2 实例使用 AMD Virtex UltraScale+ HBM VU47P，每 FPGA 带 16GB HBM2e + 64GB DDR4**——**云上首次具备 HBM**，主交付（≥50 token/s）不必再依赖实体卡。

### 5.1.0 F1 → F2 变迁说明（为什么"换芯片"）

**澄清：FPGA 厂商并未切换——F1 与 F2 都是 AMD/Xilinx 器件**（F1 = Xilinx VU9P，F2 = AMD VU47P，同属 UltraScale+ 家族）。真正的变化有两处：

| 维度 | F1（2016 发布） | F2（2024-12 发布） | 变化原因 |
| --- | --- | --- | --- |
| FPGA 器件 | Xilinx VU9P（16nm，~2.5M 逻辑、~6.8K DSP） | **AMD VU47P（16nm，~2.3M 逻辑、~5.7K DSP，含 HBM）** | **加 HBM**：F1 只有 64GB DDR4（4 通道 288-bit，数十 GB/s 级），带宽是 FPGA 加速的头号瓶颈；VU47P 增加 **16GB HBM2e（~460GB/s，约为 DDR4 的 5 倍）**并保留 64GB DDR4 兜底容量——带宽敏感负载（AI 推理、数据库、网络）提速数量级。代价是逻辑/DSP 略减（HBM 封装占面积），AWS 用"少一点逻辑、多 16GB HBM"换取带宽，对 AI/大数据类负载更划算 |
| 主机 CPU | Intel Xeon（Broadwell 代，2016） | **3 代 AMD EPYC（Milan）** | AWS 全平台趋势（2018 年起大量实例用 EPYC，性价比/核数/内存带宽优势），与 FPGA 器件无关；F2 借此把 vCPU 64→192、内存、NVMe、网络（100Gbps）全面翻倍 |
| 平台定位 | 云上 FPGA 首秀（2016） | "第二代"：官方称性价比提升至 60% | F1 平台服役 8 年整体换代；AI 推理对"定制数据类型+低延迟+可重构"的需求让 FPGA 云重新受关注（F2 = 首个带 16GB HBM 的 FPGA 云实例） |

> 对本项目的影响：F2 与本地目标平台（VHK158/VU35P 等）同为 AMD 器件族、工具链统一（Vivado），vdot RTL 可移植性与迁移成本最优。

### 5.1.1 AWS F2 实例详细参数（2026 调研）

**器件核实（2026-08，回应"F2 是否为 Intel Agilex 7"的疑问）**：**AWS F2 使用 AMD Virtex UltraScale+ HBM VU47P，不是 Intel Agilex 7**。证据链（多方交叉，含代码级证据）：
1. AWS 官方实例页（[中文](https://aws.amazon.com/cn/ec2/instance-types/f2/)/[英文](https://aws.amazon.com/ec2/instance-types/f2/)）："powered by up to 8 **AMD Virtex UltraScale+ HBM VU47P** FPGAs"；
2. [AWS F2 FPGA Developer Kit 官方文档](https://awsdocs-fpga-f2.readthedocs-hosted.com/latest/User-Guide-AWS-EC2-FPGA-Development-Kit.html)："With **AMD UltraScale+ VU47P FPGAs** and High Bandwidth Memory (HBM)..."；
3. [aws-fpga 仓库](https://github.com/aws/aws-fpga)（默认分支 `f2`）F2 Shell 构建脚本实测器件串：`set DEVICE_TYPE "xcvu47p-fsvh2892-2-e"`；
4. [AWS re:Post 问答](https://www.repost.aws/pt/questions/QUNgYPmGxkSDmFQ2qK6i09jg/)讨论"F2 实例 + **Vivado**（AMD/Xilinx 工具链）"——与 AMD 器件一致（若为 Intel 器件应用 Quartus）。
> "F2 用 Intel Agilex 7"的说法未在任何权威来源找到支持（疑与 F1 的 AMD 血统、或阿里云 f5（Intel FPGA）等其他平台混淆）；若您有反证来源请提供，我们将更新本文档。

**器件**：AMD **XCVU47P**（Virtex UltraScale+ HBM，封装 fsvh2892）：16GB HBM2e、~2.3M 逻辑单元、~5.7K DSP（以 AMD 数据表为准）；每 FPGA 另有 64GB DDR4。F2 主机为第三代 AMD EPYC（Milan）。可用性：2024-12 发布（f2.12xlarge/f2.48xlarge），2025-02 f2.6xlarge GA，2025 年扩展至多个区域（[发布动态](https://www.storagenewsletter.com/2025/01/02/availability-of-second-gen-fpga-powered-amazon-ec2-instances-f2/)、[区域扩展](https://www.westloop.io/post/amazon-ec2-f2-instances-are-now-generally-available-in-four-additional-aws-regions)）。

| 实例 | FPGA 数 | vCPU | FPGA 内存（HBM / DDR4） | 实例内存 | 本地 NVMe | 网络 | **按量价（us-east-1）** |
| --- | --- | --- | --- | --- | --- | --- | --- |
| **f2.6xlarge** | 1 | 24 | **16 GiB / 64 GiB** | 256 GB | 1×940 GiB | 12.5 Gbps | **$1.98/h ≈ ¥14.2/h**（[价格参考](https://www.devzero.dev/instances/aws/f2.6xlarge)） |
| f2.12xlarge | 2 | 48 | 32 GiB / 128 GiB | 512 GB | 2×940 GiB | 25 Gbps | $3.96/h ≈ ¥28.3/h（[参考](https://www.devzero.dev/instances/aws/f2.12xlarge)） |
| f2.48xlarge | 8 | 192 | **128 GiB / 512 GiB** | 2 TiB | 8×940 GiB | 100 Gbps | $15.84/h ≈ ¥113.3/h（[参考](https://www.devzero.dev/instances/aws/f2.48xlarge)） |

其他云：**阿里云 FPGA 实例**——服务状态待最终确认（见下）；历史型号 F3（VU9P，无 HBM）¥17.5/h 级、月付 ¥5,040/月（[F3 促销页](https://promotion.aliyun.com/ntms/act/fpgaf3.html)）。
> **阿里云 FaaS/FPGA 实例状态（2026-08 核实）**：官方 F3 促销页仍在线；代理渠道（[典名科技 2025-09-23 更新](https://www.023.cn/news/729445.html)）仍列出 **f5（据称 Intel FPGA）与 f3（Xilinx VU9P）两系列报价**（f3 4核16G 月费 ¥3,649、f5 32核128G 月费 ¥5,172 等）——**大概率仍在售**，但阿里云产品页/控制台为动态页面无法直接核验可购买性，**下单前须在阿里云控制台或工单确认**（此类实例常有区域/邀测/配额限制）。
> spot 价格通常为按量的 30–70%，以 AWS 控制台实时价为准；F2 全系用同一 F2 HDK/AFI 流程（[AWS F2 官方页](https://aws.amazon.com/cn/ec2/instance-types/f2/)、[F2 发布公告](https://aws.amazon.com/jp/blogs/aws/now-available-second-generation-fpga-powered-amazon-ec2-instances-f2/)）。

### 5.1.2 租 vs 买：场景量化对比（F2 基准）

| 场景 | 云上 F2 | 购买实体卡 |
| --- | --- | --- |
| 非 24/7 开发（160h/月 × 6 月） | **f2.6xlarge ≈ ¥13,632**（spot 更低） | U55C+工作站 ¥102,828 / VHK158+工作站 ¥138,361（一次性） |
| 24/7 常驻 3 个月（2,160h） | f2.6xlarge ≈ ¥30,672 | 同上 |
| **主交付演示（≥50 token/s）** | **f2.6xlarge 单 FPGA 即可**（16GB HBM + 64GB DDR4 放权重） | VHK158+工作站 ¥138,361 |
| **全量 284B（INT2/INT4）** | **f2.48xlarge（8 FPGA：128GB HBM + 512GB DDR4）**，演示/基准 200h ≈ **¥22,655** | 8× VHK158+服务器 ≈ ¥110 万 |

**判断（F2 时代结论反转）**：
- **开发/验证 + 主交付演示**：租 F2 全面优于购买——6 个月非 24/7 仅 **¥1.4 万**（购卡的 ~10%），且主交付 HBM 需求云上可满足。
- **持续 24/7 生产 >6–12 个月**：购置实体卡折旧更划算（云按量 ~¥14/h 年化 ≈ ¥12 万+）。
- **全量 284B**：f2.48xlarge 按需按小时租，演示/评测成本极低（¥2–5 万量级），完全无需百万级购卡。

### 5.1.3 推荐路线（F2 时代，纯云优先）

```
阶段一~三（0–3 月）：T0 云主机/本地 ≈ ¥2 万（无 FPGA）
阶段四 开发验证（3–6 月）：AWS F2（f2.6xlarge 按量/spot）≈ ¥1–1.5 万
阶段四 主交付演示（≥50 token/s）：f2.6xlarge 单 FPGA（16GB HBM+64GB DDR4）按小时
全量 284B 加分演示：f2.48xlarge 按小时（200h ≈ ¥2.3 万）
──────────────────────────────────────────────
纯云方案总投入：≈ ¥5–8 万（全部按量，无实体资产）
实体卡混合（可选）：1× VHK158 ≈ ¥11.8 万（长期自用/转售）
```

### 5.1.4 云 FPGA 的工程注意点（F2）

1. **工具链**：F2 用统一 **F2 FPGA Developer Kit / AFI 流程**（[aws-fpga 仓库](https://github.com/aws/aws-fpga)，默认分支已切 F2），Shell 固定（CL 占 ~88% 资源）；与本地 Vitis 流程不同，vdot 通路可迁移但驱动/DMA 需适配（+1–2 周）。
2. **F2 器件差异**：VU47P 与本地 VHK158（Versal HBM）/U55C（VU35P）器件族不同——**vdot RTL 可移植，但需重新综合/时序收敛**；建议把 F2 作为**目标平台之一**纳入 11 文档的选型（云上 F2 与实体 Versal 可并存）。
3. **综合/布局布线在 CPU 实例跑**（便宜），FPGA 实例只在**上板测试**时开机（省 ~90%）。
4. **spot 可中断**：适合 CI/回归，不适合长时间演示（演示用按量）。
5. **数据安全/合规**：权重与代码上云评估合规（大赛作品开源，通常无碍）。
6. **可用区/配额**：F2 属加速计算实例，需申请配额（vCPU 上限），提前开通。

## 6. 参考来源

- [东南大学 Alveo U55C 竞价结果公告](https://www.dingbiao.com/zhaobiao/70186c651c1cfc0ba73ee4830b6389ee.html)（成交价 ¥82,828）
- [ICSuns EK-VHK158-G 报价](https://www.icsuns.com/zh-cn/product/ek-vhk158-g/22147154.html)（$16,554）；[Farnell 在售页](https://de.farnell.com/en-DE/amd/ek-vhk158-g-j/evaluation-kit-arm-cortex-a72/dp/4531168)
- [AMD Versal HBM VHK158 官方页](https://www.amd.com/zh-cn/products/adaptive-socs-and-fpgas/evaluation-boards/vhk158.html)
- [AMD Alveo U55C 官方页](https://www.amd.com/zh-cn/products/accelerators/alveo/u55c/a-u55c-p00g-pq-g.html)
- [Vivado ML Enterprise 许可价格讨论（AMD 论坛）](https://adaptivesupport.amd.com/s/question/0D54U00005wRQ4cSAG/)
- **AWS F2**：[官方实例页](https://aws.amazon.com/cn/ec2/instance-types/f2/)（VU47P、HBM/DDR4 规格表）｜[发布公告（AWS News Blog）](https://aws.amazon.com/jp/blogs/aws/now-available-second-generation-fpga-powered-amazon-ec2-instances-f2/)｜[F2 FPGA Developer Kit（官方文档确认 VU47P）](https://awsdocs-fpga-f2.readthedocs-hosted.com/latest/User-Guide-AWS-EC2-FPGA-Development-Kit.html)｜[aws-fpga 仓库（默认分支 f2，HDK 器件串 xcvu47p）](https://github.com/aws/aws-fpga)｜[F2 价格 f2.6xlarge](https://www.devzero.dev/instances/aws/f2.6xlarge) / [f2.12xlarge](https://www.devzero.dev/instances/aws/f2.12xlarge) / [f2.48xlarge](https://www.devzero.dev/instances/aws/f2.48xlarge)｜[F2+Vivado 问答（re:Post）](https://www.repost.aws/pt/questions/QUNgYPmGxkSDmFQ2qK6i09jg/)｜[F2 可用性/区域扩展](https://www.storagenewsletter.com/2025/01/02/availability-of-second-gen-fpga-powered-amazon-ec2-instances-f2/)
- **F1 退役**：[f1 实例状态（runs-on）](https://go.runs-on.com/instances/ec2/f1)、[re:Post 问答](https://repost.aws/ko/questions/QUNaAz4HY5TSO7_SBNRy1mtA/help-with-f1-2xlarge-instance-launch)
- **阿里云 FPGA 实例**：[F3 促销页](https://promotion.aliyun.com/ntms/act/fpgaf3.html)（VU9P、邀测信息）；[代理商报价更新（2025-09，f3/f5 两系列）](https://www.023.cn/news/729445.html)；可购买性须以阿里云控制台/工单为准
- 配置与性能依据：[11_FPGA加速平台与硬件配置方案](./11_FPGA加速平台与硬件配置方案.md)
