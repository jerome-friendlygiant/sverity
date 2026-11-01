// sverity-args: --top top --slang-default-warnings
// expect-coverage: upstream-diagnostics INCOMPLETE
module top(input logic a, output logic y);
  assign y = a;
endmodule
