// sverity-args: --top top -Wno-width-trunc
// The option silences slang's truncation diagnostic; the run discloses that its upstream diagnostics are incomplete.
// expect-coverage: upstream-diagnostics INCOMPLETE
// expect-none: LNT-004
module top(input logic [7:0] a, output logic [3:0] y);
  assign y = a;
endmodule
