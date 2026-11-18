// sverity-args: --top top
// A port connection expression re-evaluates on every change of its operands: the function it calls keeps `s` between evaluations.
// expect: LNT-013 WARNING 10
module pass(input [3:0] i, output [3:0] o);
  assign o = i;
endmodule
module top(input [3:0] a, output [3:0] y);
  function [3:0] acc(input [3:0] v);
    reg [3:0] s;
    begin s = s ^ v; acc = s; end
  endfunction
  pass u(.i(acc(a)), .o(y));
endmodule
