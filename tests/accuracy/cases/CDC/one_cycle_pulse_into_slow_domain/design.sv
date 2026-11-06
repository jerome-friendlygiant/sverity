module top(input logic src_clk,
  dst_clk,
  fire,
  output logic pulse_dst);

  logic pulse_src, meta;
  always_ff @(posedge src_clk) pulse_src <= fire;
  always_ff @(posedge dst_clk) begin
    meta <= pulse_src;
    pulse_dst <= meta;
  end

endmodule
