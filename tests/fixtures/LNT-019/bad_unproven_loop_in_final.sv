// sverity-args: --top top
// A final block is not synthesized, but the loop condition is not proven to progress: undecided, so a WARNING, never INFO.
// expect: LNT-019 WARNING 6
// expect-none: LNT-019 INFO
module top(input logic clk, q);
  final while (q) $display("q");
endmodule
