// sverity-args: --top top
// A default argument that is used is assigned to its formal: its X bits are an assigned value, even as a void function statement.
// expect: LNT-039 WARNING 5
module top(input logic clk, output logic [1:0] y);
  function automatic void f(input logic [1:0] a = 2'bx1); y = a; endfunction
  always_comb f();
endmodule
