// sverity-args: --top top
// A task writes module state `a` from `b` as a side effect and `b` reads `a`: the call may close a loop (Unresolved).
// expect: LNT-012 WARNING
// expect-none: LNT-012 ERROR
module top(input logic x, output logic y);
  logic a, b;
  task automatic t(); a = ~b; endtask
  always_comb t();
  assign b = a & x;
  assign y = b;
endmodule
