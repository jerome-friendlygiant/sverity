// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT

module top #(
  parameter int unsigned AddrW = 9,
  parameter int unsigned DataW = 40
) (
  input  logic             clk_i,
  input  logic             rst_ni,
  input  logic             sample_i,
  input  logic [DataW-1:0] sample_data_i,
  input  logic             read_i,
  input  logic [AddrW-1:0] read_addr_i,
  output logic [DataW-1:0] read_data_o,
  output logic [AddrW-1:0] fill_level_o
);

  logic [AddrW-1:0] wptr_q;
  logic [DataW-1:0] macro_q;
  logic             rd_pulse_q;

  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (!rst_ni) begin
      wptr_q     <= '0;
      rd_pulse_q <= 1'b0;
    end else begin
      rd_pulse_q <= read_i;
      if (sample_i) wptr_q <= wptr_q + 1'b1;
    end
  end

  sram_hd_2p_512x40 u_macro (
    .CLKA (clk_i),
    .WEA  (sample_i),
    .ADDRA(wptr_q),
    .DA   (sample_data_i),
    .CLKB (clk_i),
    .REB  (read_i),
    .ADDRB(read_addr_i),
    .QB   (macro_q)
  );

  assign read_data_o  = rd_pulse_q ? macro_q : '0;
  assign fill_level_o = wptr_q;

endmodule
