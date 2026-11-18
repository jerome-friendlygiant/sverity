// sverity-args: --top top
// The task can return before it clears the register.
// expect: LNT-054 WARNING 9
module top(input logic clk, rst, skip, d, output logic y);
  logic q;
  task automatic clear(); if (skip) return; q <= 1'b0; endtask
  always_ff @(posedge clk) if (rst) clear(); else q <= d;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
// revert: if (skip) return; q <= 1'b0; => q <= 1'b0;
// revert-expect-none: LNT-054
