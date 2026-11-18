// sverity-args: --top top
// The condition calls a function that returns the register: the call reads it, so its unknown value steers the control flow.
// expect: LNT-054 WARNING 8
module top(input logic clk, d, output logic y);
  logic q;
  function automatic logic test(); return q; endfunction
  always @(posedge clk) q <= d;
  always_comb if (test()) y = d; else y = 0;
endmodule
