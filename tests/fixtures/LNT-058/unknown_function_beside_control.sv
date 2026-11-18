// sverity-args: --top top
// The test calls a function that reads module state: whether it depends on more than rst_n is not read, so it is never silent.
// expect: LNT-058 WARNING
module top(input logic clk, rst_n, x, d, output logic q);
  function automatic logic f(input logic a); return a & x; endfunction
  always_ff @(posedge clk or negedge rst_n) if (!rst_n || f(1'b1)) q <= 1'b0; else q <= d;
endmodule
