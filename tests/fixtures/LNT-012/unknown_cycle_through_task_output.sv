// sverity-args: --top top
// A task writes `a` from its output argument and `b` reads `a`: the call may close a loop, which is not decided (Unresolved, not ERROR).
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic x, output logic y);
  logic a, b;
  task automatic t(output logic o, input logic i); o = ~i; endtask
  always_comb t(a, b);
  assign b = a & x;
  assign y = b;
endmodule
