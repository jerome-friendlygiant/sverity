module top(input logic src_clk,
  dst_clk,
  step,
  output logic terminal_dst);

  logic [2:0] phase = '0;
  logic decoded, meta;
  always_ff @(posedge src_clk) if (step) phase <= phase + 1'b1;
  assign decoded = (phase == 3'd3);
  always_ff @(posedge dst_clk) begin
    meta <= decoded;
    terminal_dst <= meta;
  end

endmodule
