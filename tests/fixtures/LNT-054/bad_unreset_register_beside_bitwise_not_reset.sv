// sverity-args: --top top
// One register of the block is outside the bitwise-NOT reset branch: it has no reset and steers a condition.
// expect: LNT-054 WARNING 15
// expect-count: LNT-054 WARNING 1
module top(input logic clk_i, rst_ni, d, output logic y);
  logic stopped_q, wipe_q;
  always_ff @(posedge clk_i or negedge rst_ni) begin
    if (~rst_ni) begin
      stopped_q <= 1'b0;
    end else begin
      stopped_q <= d ^ stopped_q;
    end
  end
  always_ff @(posedge clk_i) wipe_q <= d;
  assign y = (!stopped_q && wipe_q) ? d : 1'b0;
endmodule
