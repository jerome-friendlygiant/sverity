// sverity-args: --top top
// A static local keeps its value between calls: it is persistent state the summary cannot follow.
// expect: CST-010 WARNING
module top(input logic clk, d, output logic q);
  function static logic f(logic x);
    static logic last = 0;
    logic r;
    r = x ^ last;
    last = x;
    return r;
  endfunction
  always_ff @(posedge clk) q <= f(d);
endmodule
