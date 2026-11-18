// sverity-args: --top top
// expect: LNT-074 INFO 5
// expect-count: LNT-074 INFO 1
module top(input logic [63:0] a, output logic y);
  assign y = a == 'h1_0000_0000;
endmodule
