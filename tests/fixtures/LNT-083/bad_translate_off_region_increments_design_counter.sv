// sverity-args: --top top
// The region increments a counter the design reads: synthesis builds the counter without the increment. A region that only
// writes its own declarations is a checker.
// expect: LNT-083 WARNING 11
// expect-none: LNT-083 INFO
// revert: cnt++; => seen_n++;
// revert-expect-none: LNT-083 WARNING
module top(input logic clk, input logic d, output logic [3:0] q);
  logic [3:0] cnt;
  initial cnt = 0;
  // synopsys translate_off
  logic seen;
  logic [3:0] seen_n;
  always @(posedge clk) begin seen <= d; cnt++; end
  // synopsys translate_on
  always_ff @(posedge clk) q <= cnt;
endmodule
