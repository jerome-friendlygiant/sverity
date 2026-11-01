// sverity-args: --top top
// clock plus an asynchronous reset tested by the first if; set + reset coded as an if / else-if chain
// expect-none: LNT-069
module top(input logic clk, rst_n, set_n, a, output logic q, r);
  always_ff @(posedge clk or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= a;
  always_ff @(posedge clk or negedge rst_n or negedge set_n)
    if (!rst_n) r <= 1'b0; else if (!set_n) r <= 1'b1; else r <= a;
endmodule
