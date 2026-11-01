// sverity-args: --top top
// an x drive is not a release: two drivers that can both drive x conflict
// expect: LNT-009 WARNING
module top(input logic en, a, b, output wire w);
  assign w = en ? a : 1'bx;
  assign w = !en ? b : 1'bx;
endmodule
