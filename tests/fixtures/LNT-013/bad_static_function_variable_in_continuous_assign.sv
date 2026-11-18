// sverity-args: --top top
// A static function variable read first, in a function a continuous assignment calls: it holds state between evaluations.
// expect: LNT-013 WARNING 7
module top(input [3:0] a, output [3:0] y);
  function [3:0] f(input [3:0] v);
    reg [3:0] s;
    begin s = s + v; f = s; end
  endfunction
  assign y = f(a);
endmodule
