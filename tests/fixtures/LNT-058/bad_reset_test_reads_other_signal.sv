// sverity-args: --top top
// The reset test reads x, which the event list does not watch: simulation resets only when x is low at the edge, synthesis always.
// expect: LNT-058 ERROR 5
module top(input logic clk, rst_n, x, d, output logic q);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n && !x) q <= 1'b0;
    else q <= d;
endmodule
