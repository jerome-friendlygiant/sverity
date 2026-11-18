// sverity-args: --top top
// revert: output logic [3:0] a => output logic [7:0] a
// revert-expect-none: LNT-004
// expect: LNT-004 WARNING
module top(input logic [7:0] b, output logic [3:0] a);
  assign a = b;
endmodule
