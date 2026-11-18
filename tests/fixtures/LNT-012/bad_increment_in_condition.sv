// sverity-args: --top top
// x++ in the condition reads and writes x before the branch: the same loop as the statement form.
// expect: LNT-012 ERROR
module top(input logic [3:0] a, output logic [3:0] y);
  logic [3:0] x;
  always_comb begin if (x++ == 0) y = a; else y = 0; end
endmodule
