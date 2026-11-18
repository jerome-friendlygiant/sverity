// sverity-args: --top top
// The reset branch writes q only when `c` holds, so q has no reset value on the other path.
// expect: LNT-067 INFO 6
module top(input logic clk, rst, c, input logic [7:0] d, output logic [7:0] y);
  logic [7:0] q;
  always_ff @(posedge clk) if (rst) begin if (c) q <= '0; end else q <= d;
  assign y = q;
endmodule
// revert: if (c) q <= '0; => q <= '0;
// revert-expect-none: LNT-067
