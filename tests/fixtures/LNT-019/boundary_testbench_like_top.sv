// sverity-args: --top tb
// A top-level module without ports is testbench-like: non-synthesizable constructs are still listed, as INFO.
// expect: LNT-019 INFO
// expect-none: LNT-019 WARNING
module tb;
  logic clk = 0;
  always #5 clk = ~clk;
endmodule
