// sverity-args: --top top
// expect: LNT-019 WARNING 5
module top(input logic clk, input logic [3:0] n, output logic [3:0] q);
  function automatic int f(int x); return x; endfunction
  always_ff @(posedge clk) for (int i = 0; i < f(n); i++) q[i] <= 1'b1;
endmodule
