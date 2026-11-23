module top(input logic clk_src, clk_dst, event_in, output logic event_out);
  logic stage1, stage2;
  always_ff @(posedge clk_dst) begin
    stage1 <= event_in;
    stage2 <= stage1;
  end
  assign event_out = stage2;
endmodule
