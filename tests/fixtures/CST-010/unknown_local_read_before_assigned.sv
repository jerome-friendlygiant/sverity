// sverity-args: --top top
// Lookalike of good_automatic_local_in_function: when a is 0 the local is read unassigned, so the value is not established.
// expect: CST-010 WARNING
module top(input logic clk, sel, output logic q);
  function automatic logic f(input logic a);
    logic x;
    if (a) x = 1'b1;
    return x;
  endfunction
  always_ff @(posedge clk) q <= f(sel);
endmodule
