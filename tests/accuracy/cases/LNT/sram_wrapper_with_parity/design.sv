// Authored for the SVerity accuracy suite. SPDX-License-Identifier: MIT
module top (
  input  logic        clk_i,
  input  logic        req_i,
  input  logic        we_i,
  input  logic [9:0]  addr_i,
  input  logic [31:0] wdata_i,
  output logic [31:0] rdata_o,
  output logic        parity_err_o
);
  logic        wr_parity;
  logic [32:0] mem_rdata;
  logic        rd_pending_q;

  assign wr_parity = ^wdata_i;

  sram_macro_1p #(
    .ADDR_W(10),
    .DATA_W(33)
  ) u_mem (
    .clk  (clk_i),
    .cs   (req_i),
    .we   (we_i),
    .addr (addr_i),
    .din  ({wr_parity, wdata_i}),
    .dout (mem_rdata)
  );

  always_ff @(posedge clk_i) begin
    rd_pending_q <= req_i & ~we_i;
  end

  assign rdata_o      = mem_rdata[31:0];
  assign parity_err_o = rd_pending_q & (^mem_rdata);
endmodule
