// sverity-args: --top top -Wno-sign-compare
// The option silences slang's sign-compare diagnostic; the run discloses that its upstream diagnostics are incomplete.
// expect-coverage: upstream-diagnostics INCOMPLETE
// expect-none: LNT-005
module top(input logic signed [7:0] a, input logic [7:0] b, output logic y);
  assign y = a < b;
endmodule
