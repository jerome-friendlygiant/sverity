// sverity-args: --top top --sdc @DIR@/malformed.sdc
// A malformed constraint (create_clock without -period) is reported, never dropped.
// expect: CST-018 WARNING
// expect-coverage: constraints INCOMPLETE
module top(input logic clk, rst_n, d, output logic q);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
