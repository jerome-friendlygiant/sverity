// Flags a one-wire bus that has been left floating for 15 cycles.
// Interface:
//   line_in    sampled bus line
//   float_cnt  consecutive floating cycles, saturates at 15
//   floating   set once the count has saturated
// The line is released by all drivers when the bus is idle.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       line_in,
  output logic       floating,
  output logic [3:0] float_cnt
);
  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      float_cnt <= 4'd0;
      floating  <= 1'b0;
    end else if (line_in === 1'bz) begin
      if (float_cnt != 4'hF) float_cnt <= float_cnt + 4'd1;
      else                   floating  <= 1'b1;
    end else begin
      float_cnt <= 4'd0;
      floating  <= 1'b0;
    end
  end
endmodule
