// sverity-args: --top top
// `rst_q` may be a registered reset or a flag: its identity is undecided, so `busy` is neither called reset nor unreset.
// expect: LNT-054 WARNING 11
module top(input logic clk, start, rst_q, d, output logic y);
  logic busy;
  always_ff @(posedge clk) begin
    if (start) busy <= 1'b1;
    if (rst_q) busy <= 1'b0;
  end
  always_comb begin
    if (busy) y = d; else y = 1'b0;
  end
endmodule
