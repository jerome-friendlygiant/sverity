# Reviewed clock intent for OpenTitan top_earlgrey at 92f3d03b0dcf6d5c1933a65d1497747f67d19f4c
# (SYNTHESIS, generic prims, with top_earlgrey.cells.json declaring prim_clock_gating a latched clock gate).
# One clock per modeled root. Families are mutually asynchronous, after OpenTitan hw/top_earlgrey/syn/chip_earlgrey_asic.sdc
# group1 (main, usb, spi_device, io, io_div2, io_div4, jtag, aon) plus the two I3C buses, whose pad clocks have no
# relationship to the system clocks. Within a family the relation is left unknown: a clock select and its oscillator, a
# clock and its inverted copy, or two pad signals are not proven to be the same clock. The external AST clocks are in no
# group: in bypass mode the AST selects them as the main, I/O and USB clocks (ast_clks_byp_primary), and the upstream
# constraints leave group_ast out for CDC for that reason, so their relation to every other clock stays unknown.
set_units -time ns

# clkmgr divide-by-4 of the I/O clock
create_clock -name IO_DIV4 -period 41.664 [get_pins {earlgrey_pd_aon/u_clkmgr/u_no_scan_io_div4_div/u_clk_mux/clk_o}]
# SPI device SCK (scan mux, functional input)
create_clock -name SPI_DEV -period 20.0 [get_pins {earlgrey_pd_main/u_spi_device/u_clk_spi_in_mux/clk_o}]
# SPI device chip select used as a clock
create_clock -name SPI_DEV_CSB -period 40.0 [get_pins {earlgrey_pd_main/u_spi_device/u_clk_csb_mux/clk_o}]
# SPI device inverted SCK for output
create_clock -name SPI_DEV_OUT -period 20.0 [get_pins {earlgrey_pd_main/u_spi_device/u_clk_spi_out_mux/clk_o}]
# AST I/O clock select; drives clkmgr io root
create_clock -name IO -period 10.416 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/u_clk_src_io_sel/clk_o}]
# AST always-on clock select
create_clock -name AON -period 5000.0 [get_pins {earlgrey_pd_aon/u_ast_part_secondary/u_ast_clks_byp_secondary/u_clk_src_aon_sel/clk_o}]
# I3C target inverted SCL
create_clock -name I3C0_SCL_N -period 80.0 [get_pins {earlgrey_pd_main/u_i3c0_part_primary/u_core/u_targ_inbufs/u_scl_inv/u_mux/clk_o}]
# I3C target SDA used as a clock
create_clock -name I3C0_SDA -period 80.0 [get_pins {earlgrey_pd_main/u_i3c0_part_primary/u_core/u_targ_inbufs/u_sda0_clk/u_mux/clk_o}]
# I3C target inverted SDA used as a clock
create_clock -name I3C0_SDA_N -period 80.0 [get_pins {earlgrey_pd_main/u_i3c0_part_primary/u_core/u_targ_inbufs/u_sda0_clk_n/u_mux/clk_o}]
# I3C target SCL from the pad
create_clock -name I3C0_SCL -period 80.0 [get_pins {earlgrey_pd_main/u_i3c0_part_primary/u_core/u_targ_inbufs/u_scl_buf/u_mux/clk_o}]
# I3C target inverted SCL
create_clock -name I3C1_SCL_N -period 80.0 [get_pins {earlgrey_pd_main/u_i3c1_part_primary/u_core/u_targ_inbufs/u_scl_inv/u_mux/clk_o}]
# I3C target SDA used as a clock
create_clock -name I3C1_SDA -period 80.0 [get_pins {earlgrey_pd_main/u_i3c1_part_primary/u_core/u_targ_inbufs/u_sda0_clk/u_mux/clk_o}]
# I3C target inverted SDA used as a clock
create_clock -name I3C1_SDA_N -period 80.0 [get_pins {earlgrey_pd_main/u_i3c1_part_primary/u_core/u_targ_inbufs/u_sda0_clk_n/u_mux/clk_o}]
# I3C target SCL from the pad
create_clock -name I3C1_SCL -period 80.0 [get_pins {earlgrey_pd_main/u_i3c1_part_primary/u_core/u_targ_inbufs/u_scl_buf/u_mux/clk_o}]
# I3C reset detector SDA used as a clock
create_clock -name I3C1_RSTDET_SDA -period 80.0 [get_pins {earlgrey_pd_main/u_i3c1_part_secondary/u_reset_detector/u_targ_inbufs/u_sda0_clk/u_mux/clk_o}]
# I3C reset detector SCL
create_clock -name I3C1_RSTDET_SCL -period 80.0 [get_pins {earlgrey_pd_main/u_i3c1_part_secondary/u_reset_detector/u_targ_inbufs/u_scl_buf/u_mux/clk_o}]
# I3C reset detector inverted SDA
create_clock -name I3C1_RSTDET_SDA_N -period 80.0 [get_pins {earlgrey_pd_main/u_i3c1_part_secondary/u_reset_detector/u_targ_inbufs/u_sda0_clk_n/u_mux/clk_o}]
# AST system clock select (oscillator or bypass); drives clkmgr main root and its gated copies
create_clock -name MAIN -period 10.0 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/u_clk_src_sys_sel/clk_o}]
# life-cycle TAP TCK
create_clock -name JTAG_LC -period 33.3 [get_pins {earlgrey_pd_main/u_lc_ctrl/gen_tap_tlul/u_prim_clock_mux2/clk_o}]
# life-cycle TAP inverted TCK
create_clock -name JTAG_LC_N -period 33.3 [get_pins {earlgrey_pd_main/u_lc_ctrl/gen_tap_tlul/u_dmi_jtag/i_dmi_jtag_tap/i_tck_inv/gen_scan/i_dft_tck_mux/clk_o}]
# AST USB clock select; drives clkmgr usb root
create_clock -name USB -period 20.8 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/u_clk_src_usb_sel/clk_o}]
# AST system oscillator model output, before the bypass select
create_clock -name MAIN_OSC -period 10.0 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_sys_clk/u_sys_osc/clk}]
# AST I/O oscillator model output
create_clock -name IO_OSC -period 10.416 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_io_clk/u_io_osc/clk}]
# AST USB oscillator model output
create_clock -name USB_OSC -period 20.8 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_usb_clk/u_usb_osc/clk}]
# external AST clock after scan select
create_clock -name AST_EXT -period 20.832 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/clk_ast_ext_scn}]
# external clock after scan select
create_clock -name AST_EXT_SCN -period 20.832 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/clk_ext_scn}]
# external clock divided by 1 or 2 for USB
create_clock -name AST_EXT_USB -period 41.664 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/u_no_scan_clk_ext_d1ord2/u_clk_mux/clk_o}]
# inverted external USB clock
create_clock -name AST_EXT_USB_N -period 41.664 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/clk_src_ext_usb_n}]
# RISC-V debug TAP TCK
create_clock -name JTAG_RV -period 33.3 [get_pins {earlgrey_pd_main/u_rv_dm/gen_jtag_gating/u_prim_clock_mux2/clk_o}]
# RISC-V debug TAP inverted TCK
create_clock -name JTAG_RV_N -period 33.3 [get_pins {earlgrey_pd_main/u_rv_dm/gen_jtag_gating/dap/i_dmi_jtag_tap/i_tck_inv/gen_scan/i_dft_tck_mux/clk_o}]
# I3C reset detector SDA used as a clock
create_clock -name I3C0_RSTDET_SDA -period 80.0 [get_pins {earlgrey_pd_aon/u_i3c0_part_secondary/u_reset_detector/u_targ_inbufs/u_sda0_clk/u_mux/clk_o}]
# I3C reset detector SCL
create_clock -name I3C0_RSTDET_SCL -period 80.0 [get_pins {earlgrey_pd_aon/u_i3c0_part_secondary/u_reset_detector/u_targ_inbufs/u_scl_buf/u_mux/clk_o}]
# I3C reset detector inverted SDA
create_clock -name I3C0_RSTDET_SDA_N -period 80.0 [get_pins {earlgrey_pd_aon/u_i3c0_part_secondary/u_reset_detector/u_targ_inbufs/u_sda0_clk_n/u_mux/clk_o}]
# clkmgr divide-by-2 of the I/O clock
create_clock -name IO_DIV2 -period 20.832 [get_pins {earlgrey_pd_aon/u_clkmgr/u_no_scan_io_div2_div/u_clk_mux/clk_o}]
# inverted always-on clock (scan mux, functional input inverted)
create_clock -name AON_N -period 5000.0 [get_pins {earlgrey_pd_aon/u_ast_part_secondary/u_clk_aon_inv/gen_scan/i_dft_tck_mux/clk_o}]
# inverted always-on clock in the 3.3V regulator model
create_clock -name AON_H_N -period 5000.0 [get_pins {earlgrey_pd_aon/u_ast_part_secondary/u_rglts_pdm_3p3v/clk_src_aon_h_n}]
# AST always-on oscillator model output
create_clock -name AON_OSC -period 5000.0 [get_pins {earlgrey_pd_aon/u_ast_part_secondary/u_aon_clk/u_aon_osc/clk}]
# external USB clock divided by 240
create_clock -name AST_EXT_USB_DIV240 -period 9999.36 [get_pins {earlgrey_pd_main/u_ast_part_primary/u_ast_clks_byp_primary/u_no_scan_clk_usb_div240_div/u_clk_mux/clk_o}]

set_clock_groups -name earlgrey_async -asynchronous \
    -group [get_clocks {MAIN MAIN_OSC}] \
    -group [get_clocks {USB USB_OSC}] \
    -group [get_clocks {SPI_DEV SPI_DEV_CSB SPI_DEV_OUT}] \
    -group [get_clocks {IO IO_OSC}] \
    -group [get_clocks {IO_DIV2}] \
    -group [get_clocks {IO_DIV4}] \
    -group [get_clocks {JTAG_LC JTAG_LC_N JTAG_RV JTAG_RV_N}] \
    -group [get_clocks {AON AON_H_N AON_N AON_OSC}] \
    -group [get_clocks {I3C0_RSTDET_SCL I3C0_RSTDET_SDA I3C0_RSTDET_SDA_N I3C0_SCL I3C0_SCL_N I3C0_SDA I3C0_SDA_N}] \
    -group [get_clocks {I3C1_RSTDET_SCL I3C1_RSTDET_SDA I3C1_RSTDET_SDA_N I3C1_SCL I3C1_SCL_N I3C1_SDA I3C1_SDA_N}]
