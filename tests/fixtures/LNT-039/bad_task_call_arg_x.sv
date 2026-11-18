// sverity-args: --top top
// An X literal passed to an input argument is assigned to the formal, so it writes X into the value even as a statement call.
// expect: LNT-039 WARNING 6
module top(input logic clk, output logic [1:0] y);
  task automatic t(input logic [1:0] a); y = a; endtask
  always_comb t(2'bx1);
endmodule
