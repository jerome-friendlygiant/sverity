// sverity-args: --top top
// revert: always_ff q <= d; => always_ff @(posedge clk) q <= d;
// revert-expect-none: LNT-014
// always_ff needs exactly one event control (slang error mapped to LNT-014).
// expect: LNT-014 ERROR
module top(input logic clk, d, output logic q);
  always_ff q <= d;
endmodule
