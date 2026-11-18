// sverity-args: --top top
// `rst_done` is a data flag, not a reset: the last word is a status, so `busy` has no reset value.
// expect: LNT-054 WARNING 11
module top(input logic clk, start, rst_done, d, output logic y);
  logic busy;
  always_ff @(posedge clk) begin
    if (start) busy <= 1'b1;
    if (rst_done) busy <= 1'b0;
  end
  always_comb begin
    if (busy) y = d; else y = 1'b0;
  end
endmodule
