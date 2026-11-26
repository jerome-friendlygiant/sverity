// Flags the first valid beat after the bus interface leaves reset.
module top (
  input  logic clk,
  input  logic rst_ni,
  input  logic valid_i,
  output logic first_o
);
  logic       in_rst_q;
  logic [7:0] count_q;

  always_ff @(posedge clk) in_rst_q <= ~rst_ni;

  always_ff @(posedge clk or negedge rst_ni)
    if (!rst_ni) count_q <= '0;
    else if (valid_i) count_q <= count_q + 8'd1;

  assign first_o = in_rst_q & valid_i & (count_q == 8'd0);
endmodule
