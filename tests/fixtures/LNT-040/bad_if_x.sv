// sverity-args: --top top
// expect: LNT-040 WARNING 5
module top(input logic a, output logic m);
  always_comb
    if ('x) m = a; else m = 1'b0;
endmodule
