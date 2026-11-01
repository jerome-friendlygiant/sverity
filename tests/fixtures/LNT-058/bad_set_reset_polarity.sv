// sverity-args: --top top
// revert: else if (set_n) q <= 1'b1; => else if (!set_n) q <= 1'b1;
// revert-expect-none: LNT-058 ERROR
// Asynchronous reset and set in one block: the set branch is coded for an active-high signal although the event is negedge.
// expect: LNT-058 ERROR
module top(input logic clk, rst_n, set_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n or negedge set_n) if (!rst_n) q <= 1'b0; else if (set_n) q <= 1'b1; else q <= d;
endmodule
