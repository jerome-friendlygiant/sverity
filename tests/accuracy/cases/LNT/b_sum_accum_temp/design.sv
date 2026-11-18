// Saturating 8-bit accumulator.
// Interface:
//   en    accumulate din this cycle
//   din   unsigned 8-bit addend
//   sat   accumulator has reached the ceiling
//   acc   running total, holds 0xFF once saturated
// The sum is formed in a 9-bit temporary so the carry is visible.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       en,
  input  logic [7:0] din,
  output logic [7:0] acc,
  output logic       sat
);
  always_ff @(posedge clk or negedge rst_n) begin : p_acc
    logic [8:0] s;
    if (!rst_n) begin
      acc <= 8'd0;
    end else if (en) begin
      s = {1'b0, acc} + {1'b0, din};
      acc <= s[8] ? 8'hFF : s[7:0];
    end
  end

  assign sat = (acc == 8'hFF);
endmodule
