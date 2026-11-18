// sverity-args: --top top
// A net declaration assignment is a continuous assignment: the function it calls keeps `s` between evaluations.
// expect: LNT-013 WARNING 7
module top(input [3:0] a, output [3:0] y);
  function [3:0] acc(input [3:0] v);
    reg [3:0] s;
    begin s = s ^ v; acc = s; end
  endfunction
  wire [3:0] w = acc(a);
  assign y = w;
endmodule
