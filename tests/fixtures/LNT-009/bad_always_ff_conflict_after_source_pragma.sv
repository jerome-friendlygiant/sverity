// sverity-args: --top top
// The pragma lowers slang's default error to a warning; two always_ff blocks writing y are still a driver conflict.
// expect: LNT-009 ERROR
// expect: LNT-001 FATAL
`pragma diagnostic warn="-Wmultiple-always-assigns"
module top(input logic clk, a, b, output logic y);
  always_ff @(posedge clk) y <= a;
  always_ff @(posedge clk) y <= b;
endmodule
