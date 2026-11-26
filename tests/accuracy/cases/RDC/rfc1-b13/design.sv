// Chooses between the PLL lock reset and an external reset for the timer block.
module top (
  input  logic clk,
  input  logic pll_rst_ni,
  input  logic ext_rst_ni,
  input  logic ext_sel_i,
  output logic tmr_o
);
  logic       tmr_rst_n;
  logic [7:0] tmr_q;

  assign tmr_rst_n = ext_sel_i ? ext_rst_ni : pll_rst_ni;

  always_ff @(posedge clk or negedge tmr_rst_n)
    if (!tmr_rst_n) tmr_q <= '0;
    else            tmr_q <= tmr_q + 8'd1;

  assign tmr_o = tmr_q[7];
endmodule
