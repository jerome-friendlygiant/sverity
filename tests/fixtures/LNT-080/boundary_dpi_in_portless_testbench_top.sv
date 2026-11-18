// sverity-args: --top tb
// A portless top is testbench-like: foreign code there is advisory.
// expect: LNT-080 INFO 6
// expect-none: LNT-080 WARNING
module tb;
  import "DPI-C" function void report_value(input int v);
  initial report_value(1);
endmodule
