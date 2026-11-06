module top(input logic src_clk,
  dst_clk,
  level,
  output logic received);

  logic launched, meta;
  logic meta_inverted;
  always_ff @(posedge src_clk) launched <= level;
  always_ff @(posedge dst_clk) meta <= launched;
  assign meta_inverted = ~meta;
  always_ff @(posedge dst_clk) received <= meta_inverted;

endmodule
