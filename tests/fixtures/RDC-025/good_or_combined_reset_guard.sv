// sverity-args: --top top
// An OR-combined reset guard (!rst_n || !other, other || rst) is guaranteed true when the async reset asserts, so the forced constant is decided and RDC-025 stays silent.
// expect-none: RDC-025
module top(input logic clk, rst_n, rst_hi, clr, d, output logic q_lo, q_hi);
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n || clr) q_lo <= 1'b0;
    else q_lo <= d;
  always_ff @(posedge clk or posedge rst_hi)
    if (clr || rst_hi) q_hi <= 1'b1;
    else q_hi <= d;
endmodule
