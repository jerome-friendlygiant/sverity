// sverity-args: --top top
// The `break` can end the loop before the reset write runs.
// expect: LNT-054 WARNING 8
module top(input logic clk, rst, stop, d, output logic y);
  logic q;
  always_ff @(posedge clk) if (rst) begin for (int i = 0; i < 4; i++) begin if (stop) break; q <= 1'b0; end end else q <= d;
  always_comb begin
    if (q) y = d; else y = 1'b0;
  end
endmodule
// revert: if (stop) break; q <= 1'b0; => q <= 1'b0; if (stop) break;
// revert-expect-none: LNT-054
