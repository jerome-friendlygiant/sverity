// sverity-args: --top top
// The reset value comes from a function call: whether it is a constant is not decided, so it is reported as Unresolved, never silent.
// expect: LNT-070 WARNING 7
module top(input logic clk, rst_n, d, output logic q);
  function automatic logic init(input logic v); return v; endfunction
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) q <= init(d);
    else q <= d;
endmodule
