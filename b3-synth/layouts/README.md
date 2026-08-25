# B3 综合版图文件说明

本目录收录 Vdot64b 综合征/版图（GDSII + 可视化）产物，作为 B3 面积/物理评估的证据（配套 B3_REPORT.md）。

## 文件清单

| 文件 | 说明 |
| --- | --- |
| vdot_circuit.png | Vdot64b 综合后电路结构可视化（8 乘法器 + 加法树 + 累加器） |
| vdot_top.png | Vdot64b 顶层模块视图 |
| vdot64b_layout.gds | GDSII 版图文件（Yosys/OpenROAD 生成，可导入 KLayout/KiCad 查看） |
| vdot64b_layout.png | 版图整体可视化 |
| vdot64b_gds_layout.png | GDS 版图（带单元边界） |
| vdot64b_gds_sourced.png | GDS 版图（带源/金属层标注） |
| vdot64b_placed.png | 布局布线后（placed）可视化 |

## 流程

```bash
# 在 IIC-OSIC-TOOLS 容器内
yosys -s b3_final.ys              # 综合 Vdot64b.sv -> 网表
python3 -m openroad ...           # 布局布线/floorplan (可选)
# 导出 GDS + 截图如下 png
```

## 关键结论

- 单元数: 3435 cells (DFF 257 + comb 3178)
- Vdot64b 面积: 34,551 um2 (0.0346 mm2)
- VdotU 通路 (128-bit): 69,101 um2
- 相对全核: 0.014% (远低于 0.5% 目标)

详见 [../B3_REPORT.md](../B3_REPORT.md)。
