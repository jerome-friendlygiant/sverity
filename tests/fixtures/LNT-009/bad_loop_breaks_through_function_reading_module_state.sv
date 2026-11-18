// sverity-args: --top top
// The break condition is a function whose body reads a module input: it is a run-time value, so iteration 7 may write v[7].
// expect: LNT-009 ERROR 8
module top(input logic c, input logic [2:0] sel, output logic [7:0] y);
  logic [7:0] v;
  function automatic bit stop(input int k); return k == sel; endfunction
  always @(c, sel) for (int i = 0; i < 8; i++) begin if (stop(i)) break; v[i] = c; end
  always @(c) v[7] = 1'b0;
  assign y = v;
endmodule
