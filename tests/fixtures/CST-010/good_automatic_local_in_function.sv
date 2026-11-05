// sverity-args: --top top
// An automatic local holds nothing between calls and its reads are in the summary: the call is fully resolved.
// expect-none: CST-010
module top(input logic clk, d, output logic q);
  function automatic logic f(logic x);
    logic r;
    r = ~x;
    return r;
  endfunction
  always_ff @(posedge clk) q <= f(d);
endmodule
