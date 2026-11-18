// sverity-args: --top top
// The bound is a run-time value, so the loop is not unrolled and which bits of r it fills is not known: Unresolved, not state.
// expect: LNT-013 WARNING 10
module top(input [7:0] a, input [3:0] n, output reg [7:0] y);
  function [7:0] f(input [7:0] v, input [3:0] m);
    integer j;
    reg [7:0] r;
    begin
      for (j = 0; j < m; j = j + 1) r[j] = v[j];
      f = r;
    end
  endfunction
  assign y = f(a, n);
endmodule
